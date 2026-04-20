#include "vmalloc.h"
#include <ds/list.h>
#include <ds/rbtree.h>
#include <pmm.h>
#include <slab.h>
#include <sync/spinlock.h>

// ---------------------------------------------------------
// 全局状态
// ---------------------------------------------------------
static rb_tree_t va_tree;       // 全局红黑树，管理所有VMA
static spinlock_t va_tree_lock; // 保护红黑树操作的全局锁
static vmalloc_bucket_t buckets[MAX_VMALLOC_ORDER + 1]; // 分桶数组

// ---------------------------------------------------------
// 内部辅助函数
// ---------------------------------------------------------

// 红黑树比较函数：严格按虚拟起始地址比较
static int va_compare(rb_node_t *a, rb_node_t *b) {
  vmap_area_t *va_a = rb_entry(a, vmap_area_t, rb_node);
  vmap_area_t *va_b = rb_entry(b, vmap_area_t, rb_node);

  if (va_a->va_start < va_b->va_start)
    return -1;
  if (va_a->va_start > va_b->va_start)
    return 1;
  return 0;
}

// 计算需要多少页 (向上取整)
static size_t size_to_pages(size_t size) {
  return (size + PAGE_SIZE - 1) / PAGE_SIZE;
}

// 计算页数对应的最小 Order (向上取整到 2 的幂)
static unsigned int pages_to_order(size_t pages) {
  if (pages == 0)
    return 0;
  unsigned int order = 0;
  size_t p = 1;
  while (p < pages) {
    p <<= 1;
    order++;
    if (order > MAX_VMALLOC_ORDER)
      return MAX_VMALLOC_ORDER;
  }
  return order;
}

// 将空闲 VMA 放入对应的 Order 桶中
static void add_to_bucket(vmap_area_t *va) {
  size_t pages = (va->va_end - va->va_start) / PAGE_SIZE;
  unsigned int order = pages_to_order(pages);

  unsigned long flags;
  flags = spin_lock_irqsave(&va_tree_lock);
  list_add(&va->list_node, &buckets[order].free_list); // 头插法，LIFO，缓存友好
  spin_unlock_irqrestore(&buckets[order].lock, flags);
}

// 从指定的 Order 桶中移除 VMA (用于合并时清理被吃掉的块)
static void remove_from_bucket(vmap_area_t *va) {
  size_t pages = (va->va_end - va->va_start) / PAGE_SIZE;
  unsigned int order = pages_to_order(pages);
  if (order > MAX_VMALLOC_ORDER)
    order = MAX_VMALLOC_ORDER;

  unsigned long flags;
  flags = spin_lock_irqsave(&va_tree_lock);
  list_del(&va->list_node);
  spin_unlock_irqrestore(&buckets[order].lock, flags);
}

// ---------------------------------------------------------
// 对外 API 实现
// ---------------------------------------------------------

void vmalloc_init(void) {
  // 1. 初始化全局红黑树和锁
  rb_tree_init(&va_tree, va_compare);
  // 正确
  spin_lock_init(&va_tree_lock);

  // 2. 初始化所有分桶
  for (unsigned int i = 0; i <= MAX_VMALLOC_ORDER; i++) {
    // 正确
    spin_lock_init(&va_tree_lock);
    INIT_LIST_HEAD(&buckets[i].free_list);
  }

  // 3. 将整个 VMALLOC 区域初始化为一个巨大的空闲 VMA
  vmap_area_t *initial_va = (vmap_area_t *)kmalloc(sizeof(vmap_area_t));
  if (!initial_va)
    return; // 极端情况：slab 尚未就绪

  initial_va->va_start = VMALLOC_START;
  initial_va->va_end = VMALLOC_END;
  initial_va->is_free = 1;
  initial_va->phys_start = 0;
  initial_va->flags = 0;

  // 挂入红黑树
  unsigned long flags;
  flags = spin_lock_irqsave(&va_tree_lock);
  rb_tree_insert(&va_tree, &initial_va->rb_node);
  spin_unlock_irqrestore(&va_tree_lock, flags);

  // 放入空闲桶
  add_to_bucket(initial_va);
}

void vfree(void *addr) {
  if (!addr)
    return;
  u64 target_start = (u64)addr;

  // -------------------------------------------------------------
  // 1. 利用红黑树快速定位目标 VMA
  // -------------------------------------------------------------
  vmap_area_t target_node;
  target_node.va_start = target_start;

  vmap_area_t *va = NULL;
  size_t pages = 0;
  unsigned int phys_order = 0;
  phys_addr_t phys_start = 0;

  unsigned long flags;
  flags = spin_lock_irqsave(&va_tree_lock);
  rb_node_t *found_node = rb_tree_search(&va_tree, &target_node.rb_node);
  if (!found_node || rb_entry(found_node, vmap_area_t, rb_node)->is_free) {
    spin_unlock_irqrestore(&va_tree_lock, flags);
    return; // 错误：地址不存在或重复释放
  }

  va = rb_entry(found_node, vmap_area_t, rb_node);
  pages = (va->va_end - va->va_start) / PAGE_SIZE;
  phys_order = pages_to_order(pages);
  phys_start = va->phys_start;

  // 标记为空闲 (Key va_start 不变，无需操作红黑树)
  va->is_free = 1;
  va->phys_start = 0;

  // -------------------------------------------------------------
  // 2. 向后合并 (利用 rb_tree_next 找后继)
  // -------------------------------------------------------------
  rb_node_t *next_node = rb_tree_next(&va->rb_node);
  if (next_node) {
    vmap_area_t *next_va = rb_entry(next_node, vmap_area_t, rb_node);
    if (next_va->is_free && next_va->va_start == va->va_end) {
      // 吞并后继节点
      va->va_end = next_va->va_end;
      rb_tree_delete(&va_tree, &next_va->rb_node); // 后继被吃掉，从树移除
      remove_from_bucket(next_va);                 // 从桶链表移除
      kfree(next_va);
    }
  }

  // -------------------------------------------------------------
  // 3. 向前合并 (利用 rb_tree_prev 找前驱)
  // -------------------------------------------------------------
  rb_node_t *prev_node = rb_tree_prev(&va->rb_node);
  if (prev_node) {
    vmap_area_t *prev_va = rb_entry(prev_node, vmap_area_t, rb_node);
    if (prev_va->is_free && prev_va->va_end == va->va_start) {
      // 被前驱节点吞并
      prev_va->va_end = va->va_end;
      rb_tree_delete(&va_tree, &va->rb_node); // 当前节点被吃掉，从树移除
      kfree(va);                              // 释放当前节点结构体
      va = prev_va;                           // 后续操作转移给前驱节点
    }
  }

  spin_unlock_irqrestore(&va_tree_lock, flags);

  // 释放物理内存 (锁外操作，减少锁粒度)
  free_phys_pages(phys_start, phys_order);

  // -------------------------------------------------------------
  // 4. 将最终幸存的合并块放入对应大小的桶中
  // -------------------------------------------------------------
  add_to_bucket(va);
}

// ---------------------------------------------------------
// 调试打印 (标准 C 函数回调方式)
// -------------------------------------------------------------
#include <printk.h>

static void print_va_cb(rb_node_t *node, void *data __attribute__((unused))) {
  vmap_area_t *va = rb_entry(node, vmap_area_t, rb_node);
  const char *status = va->is_free ? "FREE" : "ALLOC";
  printk("[%s] va: 0x%lx - 0x%lx (phys: 0x%lx)\n", status, va->va_start,
         va->va_end, va->phys_start);
}

void vmalloc_print_layout(void) {
  unsigned long flags;
  flags = spin_lock_irqsave(&va_tree_lock);
  rb_tree_inorder(&va_tree, print_va_cb, NULL);
  spin_unlock_irqrestore(&va_tree_lock, flags);
}

// ---------------------------------------------------------
// [底层核心] 获取指定大小的空闲虚拟地址区间 (不涉及物理内存)
// ---------------------------------------------------------
// 注意：返回的 vmap_area_t
// 处于"半死不活"状态：已从空闲桶摘除，还在红黑树中，is_free=0
static vmap_area_t *__get_vm_area(size_t size) {
  if (size == 0)
    return NULL;

  size_t pages = size_to_pages(size);
  unsigned int required_order = pages_to_order(pages);
  if (required_order > MAX_VMALLOC_ORDER)
    return NULL;

  vmap_area_t *found_va = NULL;

  // 1. 遍历分桶寻找虚拟空洞 (这部分逻辑与之前完全一样)
  for (unsigned int o = required_order; o <= MAX_VMALLOC_ORDER; o++) {
    if (list_empty(&buckets[o].free_list))
      continue;
    unsigned long flags;
    flags = spin_lock_irqsave(&va_tree_lock);
    if (!list_empty(&buckets[o].free_list)) {
      found_va =
          list_first_entry(&buckets[o].free_list, vmap_area_t, list_node);
      list_del(&found_va->list_node);
      spin_unlock_irqrestore(&buckets[o].lock, flags);
      break;
    }
    spin_unlock_irqrestore(&buckets[o].lock, flags);
  }
  if (!found_va)
    return NULL;

  // 2. 切割多余的虚拟空间
  size_t found_pages = (found_va->va_end - found_va->va_start) / PAGE_SIZE;
  if (found_pages > pages) {
    vmap_area_t *new_free_va = (vmap_area_t *)kmalloc(sizeof(vmap_area_t));
    if (new_free_va) {
      new_free_va->va_start = found_va->va_start + (pages * PAGE_SIZE);
      new_free_va->va_end = found_va->va_end;
      new_free_va->is_free = 1;
      new_free_va->phys_start = 0;
      new_free_va->flags = 0;

      found_va->va_end = new_free_va->va_start;

      unsigned long flags;
      flags = spin_lock_irqsave(&va_tree_lock);
      rb_tree_insert(&va_tree, &new_free_va->rb_node);
      spin_unlock_irqrestore(&va_tree_lock, flags);
      add_to_bucket(new_free_va);
    }
  }

  // 标记为已占用，但先不设置 phys_start
  found_va->is_free = 0;
  found_va->phys_start = 0;
  return found_va;
}

// ---------------------------------------------------------
// [底层核心] 释放虚拟地址区间 (合并逻辑)
// ---------------------------------------------------------
static void __put_vm_area(vmap_area_t *va) {
  unsigned long flags;
  flags = spin_lock_irqsave(&va_tree_lock);
  va->is_free = 1;
  va->phys_start = 0;

  // 向后合并 (同之前)
  rb_node_t *next_node = rb_tree_next(&va->rb_node);
  if (next_node) {
    vmap_area_t *next_va = rb_entry(next_node, vmap_area_t, rb_node);
    if (next_va->is_free && next_va->va_start == va->va_end) {
      va->va_end = next_va->va_end;
      rb_tree_delete(&va_tree, &next_va->rb_node);
      remove_from_bucket(next_va);
      kfree(next_va);
    }
  }

  // 向前合并 (同之前)
  rb_node_t *prev_node = rb_tree_prev(&va->rb_node);
  if (prev_node) {
    vmap_area_t *prev_va = rb_entry(prev_node, vmap_area_t, rb_node);
    if (prev_va->is_free && prev_va->va_end == va->va_start) {
      prev_va->va_end = va->va_end;
      rb_tree_delete(&va_tree, &va->rb_node);
      kfree(va);
      va = prev_va;
    }
  }

  spin_unlock_irqrestore(&va_tree_lock, flags);
  add_to_bucket(va); // 放回空闲桶
}

// ---------------------------------------------------------
// [上层接口 A] vmalloc: 分配普通内核内存
// ---------------------------------------------------------
void *vmalloc(size_t size, int flags) {
  // 1. 拿到纯虚拟地址区间
  vmap_area_t *va = __get_vm_area(size);
  if (!va)
    return NULL;

  // 2. 向 Buddy 系统要物理内存
  size_t pages = (va->va_end - va->va_start) / PAGE_SIZE;
  unsigned int phys_order = pages_to_order(pages);
  phys_addr_t pa = alloc_phys_pages(phys_order);

  if (pa == 0) {
    // 物理内存分配失败，把虚拟地址退回去
    __put_vm_area(va);
    return NULL;
  }

  // 3. 绑定关系
  va->phys_start = pa;
  va->flags = flags;

  // 【关键缺失步骤】：这里应该调用底层的建页表函数，把 va->va_start 映射到 pa
  // map_kernel_pages(va->va_start, pa, pages, flags);

  return (void *)va->va_start;
}

// ---------------------------------------------------------
// [上层接口 B] ioremap: 映射设备IO内存
// ---------------------------------------------------------
// phys_addr: 设备的物理地址 (比如串口寄存器的基地址)
// size: 要映射的字节数
void *ioremap(phys_addr_t phys_addr, size_t size) {
  if (phys_addr == 0 || size == 0)
    return NULL;

  // 1. 拿到纯虚拟地址区间 (复用底层！)
  vmap_area_t *va = __get_vm_area(size);
  if (!va)
    return NULL;

  // 2. 不需要分配物理内存！直接把设备地址绑定上去
  va->phys_start = phys_addr;
  va->flags = PROT_READ | PROT_WRITE; // IO内存通常需要读写，且不带Cache

  // 【关键缺失步骤】：建立设备内存的页表映射，注意页表属性必须是非Cache的
  // map_device_pages(va->va_start, phys_addr, size, PAGE_IO_ATTR);

  return (void *)va->va_start;
}

void iounmap(void *addr) {
  if (!addr)
    return;
  vmap_area_t target_node;
  target_node.va_start = (u64)addr;

  vmap_area_t *va = NULL;
  unsigned long flags;
  flags = spin_lock_irqsave(&va_tree_lock);
  rb_node_t *found_node = rb_tree_search(&va_tree, &target_node.rb_node);
  if (!found_node || rb_entry(found_node, vmap_area_t, rb_node)->is_free) {
    spin_unlock_irqrestore(&va_tree_lock, flags);
    return;
  }
  va = rb_entry(found_node, vmap_area_t, rb_node);
  spin_unlock_irqrestore(&va_tree_lock, flags);

  // 【关键缺失步骤】：拆除设备页表映射
  // unmap_device_pages(va->va_start, va->va_end - va->va_start);

  // 归还虚拟地址区间 (注意：绝对不能调
  // free_phys_pages，因为物理地址不是Buddy给的！)
  __put_vm_area(va);
}