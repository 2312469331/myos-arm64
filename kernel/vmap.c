#include <vmap.h>
#include <printk.h>
#include <slab.h>
#include <gfp.h>// 修改后
#include <mm_defs.h>
// 确保 VMALLOC_END 已定义
#ifndef VMALLOC_END
#define VMALLOC_END (VMALLOC_START + VMALLOC_SIZE)
#endif

static struct vmap_manager vm;

static inline uint64_t va_size(struct vmap_area *va) {
    return va->va_end - va->va_start;
}

static int va_compare_size(rb_node_t *a, rb_node_t *b) {
    struct vmap_area *va_a = rb_entry(a, struct vmap_area, free_rb_node);
    struct vmap_area *va_b = rb_entry(b, struct vmap_area, free_rb_node);
    uint64_t size_a = va_size(va_a);
    uint64_t size_b = va_size(va_b);
    if (size_a < size_b) return -1;
    if (size_a > size_b) return 1;
    // 大小相同时，按起始地址排序保证红黑树唯一性
    if (va_a->va_start < va_b->va_start) return -1;
    if (va_a->va_start > va_b->va_start) return 1;
    return 0;
}

// 【修复 Bug 1】链表必须按地址从小到大排序插入，否则释放时找不到邻居！
static void va_insert_free(struct vmap_area *va) {
    struct list_head *pos;
    // 遍历找到第一个 va_start 大于当前节点 va_start 的位置
    list_for_each(pos, &vm.free_list) {
        struct vmap_area *tmp = list_entry(pos, struct vmap_area, free_list);
        if (tmp->va_start > va->va_start) {
            break;
        }
    }
    // 插入到找到的位置之前，保证链表始终按地址递增
    list_add_tail(&va->free_list, pos);
    
    rb_tree_insert(&vm.free_tree, &va->free_rb_node);
}

static void va_remove_free(struct vmap_area *va) {
    rb_tree_delete(&vm.free_tree, &va->free_rb_node);
    list_del(&va->free_list);
}

// 【优化】清理了回退查找逻辑中的指针转换混乱
static struct vmap_area *va_find_best_fit(uint64_t size) {
    struct vmap_area target;
    target.va_start = 0;
    target.va_end = size;
    
    rb_node_t *result_node = rb_tree_search(&vm.free_tree, &target.free_rb_node);
    if (result_node) {
        return rb_entry(result_node, struct vmap_area, free_rb_node);
    }
    
    // 红黑树没找到（可能红黑树实现不支持大于等于查找），退化到遍历链表找 Best-Fit
    struct vmap_area *best = NULL;
    struct vmap_area *va;
    list_for_each_entry(va, &vm.free_list, free_list) {
        if (va_size(va) >= size) {
            if (!best || va_size(va) < va_size(best)) {
                best = va;
            }
        }
    }
    return best;
}

static struct vmap_area *va_find_by_addr(uint64_t addr) {
    struct vmap_area *va;
    list_for_each_entry(va, &vm.busy_list, busy_list) {
        if (va->va_start == addr) return va;
    }
    return NULL;
}

// 因为链表已经严格按地址排序了，这里可以放心使用 else break 提前退出
static struct vmap_area *va_find_prev_free(uint64_t addr) {
    struct vmap_area *va;
    struct vmap_area *prev = NULL;
    list_for_each_entry(va, &vm.free_list, free_list) {
        if (va->va_end <= addr) prev = va;
        else break; // 遇到地址比们大的，说明后面的都不可能是前驱了
    }
    return prev;
}

static struct vmap_area *va_find_next_free(uint64_t addr) {
    struct vmap_area *va;
    list_for_each_entry(va, &vm.free_list, free_list) {
        if (va->va_start > addr) return va;
    }
    return NULL;
}

void va_manager_init(void) {
    vm.start = VMALLOC_START;
    vm.end = VMALLOC_END;
    rb_tree_init(&vm.free_tree, va_compare_size);
    INIT_LIST_HEAD(&vm.free_list);
    INIT_LIST_HEAD(&vm.busy_list);
    spin_lock_init(&vm.lock);
    vm.total_free = VMALLOC_END - VMALLOC_START;
    vm.nr_busy = 0;
    
    struct vmap_area *init_area = (struct vmap_area *)kmalloc(sizeof(struct vmap_area), GFP_KERNEL);
    if (!init_area) return;
    
    init_area->va_start = VMALLOC_START;
    init_area->va_end = VMALLOC_END;
    init_area->flags = 0;
    va_insert_free(init_area);
    // printk("[VMAP] vmap manager initialized: %lx - %lx\n", VMALLOC_START, VMALLOC_END);
}

void *va_alloc(unsigned long size, unsigned long align, unsigned long flags) {
    if (size == 0) return NULL;
    if (align < VA_ALIGN_DEFAULT) align = VA_ALIGN_DEFAULT;
    size = (size + VA_ALIGN_DEFAULT - 1) & ~(VA_ALIGN_DEFAULT - 1);
    
    unsigned long irq_flags;
    spin_lock_irqsave(&vm.lock, irq_flags);
    
    struct vmap_area *va = va_find_best_fit(size);
    if (!va) {
        spin_unlock_irqrestore(&vm.lock, irq_flags);
        // printk("[VMAP] va_alloc failed: no suitable free area for size %lu\n", size);
        return NULL;
    }
    
    // 1. 从空闲树和链表中彻底拔出
    va_remove_free(va);
    
    // 2. 处理前部对齐产生的碎片
    uint64_t aligned_start = (va->va_start + align - 1) & ~(align - 1);
    uint64_t waste = aligned_start - va->va_start;
    if (waste > 0) {
        struct vmap_area *before = (struct vmap_area *)kmalloc(sizeof(struct vmap_area), GFP_KERNEL);
        if (before) {
            before->va_start = va->va_start;
            before->va_end = aligned_start;
            before->flags = 0;
            va_insert_free(before); // 碎片插回
        }
        va->va_start = aligned_start;
    }
    
    // 3. 处理后部多余空间产生的碎片
    uint64_t va_sz = va_size(va);
    if (va_sz > size + VA_ALIGN_DEFAULT) {
        struct vmap_area *remain = (struct vmap_area *)kmalloc(sizeof(struct vmap_area), GFP_KERNEL);
        if (remain) {
            remain->va_start = va->va_start + size;
            remain->va_end = va->va_end;
            remain->flags = 0;
            va->va_end = va->va_start + size;
            va_insert_free(remain); // 碎片插回
        }
    }
    
    // 4. 核心节点加入忙碌链表
    va->flags = flags;
    list_add(&va->busy_list, &vm.busy_list);
    vm.nr_busy++;
    vm.total_free -= va_size(va);
    
    spin_unlock_irqrestore(&vm.lock, irq_flags);
    return (void *)va->va_start;
}

// 【修复 Bug 2 & 3】重构释放合并逻辑，防止红黑树崩溃和统计越界
void va_free(void *addr) {
    if (!addr) return;
    unsigned long irq_flags;
    spin_lock_irqsave(&vm.lock, irq_flags);
    
    struct vmap_area *va = va_find_by_addr((uint64_t)addr);
    if (!va) {
        spin_unlock_irqrestore(&vm.lock, irq_flags);
        // printk("[VMAP] va_free failed: address %p not found\n", addr);
        return;
    }
    
    // 从忙碌链表摘除
    list_del(&va->busy_list);
    vm.nr_busy--;
    
    // 定义最终要插回空闲空间的“候选节点”，初始就是当前释放的节点
    struct vmap_area *to_insert = va;
    
    // 1. 尝试与“前邻居”合并
    struct vmap_area *prev = va_find_prev_free(to_insert->va_start);
    if (prev && prev->va_end == to_insert->va_start) {
        // 【关键】prev 已经在红黑树和链表里了，必须先拔出来！
        va_remove_free(prev);
        // 扩展 prev 的边界覆盖 to_insert
        prev->va_end = to_insert->va_end;
        // 丢弃当前的 to_insert 节点，把 prev 作为新的候选节点
        kfree(to_insert);
        to_insert = prev;
    }
    
    // 2. 尝试与“后邻居”合并
    struct vmap_area *next = va_find_next_free(to_insert->va_end);
    if (next && to_insert->va_end == next->va_start) {
        // 【关键】next 已经在红黑树和链表里了，必须先拔出来！
        va_remove_free(next);
        // 扩展候选节点的边界覆盖 next
        to_insert->va_end = next->va_end;
        // 丢弃 next 节点
        kfree(next);
    }
    
    // 3. 统计修正：不管有没有发生合并，真正从“忙碌”变成“空闲”的只有最初那个 va 的空间。
    // prev 和 next 本来就是空闲的，它们的空间已经被统计过了，所以绝对不能再加！
    vm.total_free += va_size(va);
    
    // 4. 将最终合并好的孤立节点，统一插回红黑树和链表（此时它绝对不在里面，安全！）
    va_insert_free(to_insert);
    
    spin_unlock_irqrestore(&vm.lock, irq_flags);
}
