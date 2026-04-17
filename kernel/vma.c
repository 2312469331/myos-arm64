#include <vma.h>
#include <ds/list.h>
#include <ds/rbtree.h>
#include <printk.h>
#include <slab.h>

// 全局变量定义
vmap_manager_t free_vmap; // 全局唯一
list_head_t alloced_vm_list;

// 比较函数：按 va_start 比较
static int vmap_area_compare(const void *key1, const void *key2) {
    uint64_t addr1 = *(uint64_t *)key1;
    uint64_t addr2 = *(uint64_t *)key2;
    
    if (addr1 < addr2) {
        return -1;
    } else if (addr1 > addr2) {
        return 1;
    } else {
        return 0;
    }
}

// 初始化 vmap_manager
void vmap_manager_init(vmap_manager_t *manager, uint64_t start, uint64_t end) {
    // 初始化链表
    INIT_LIST_HEAD(&manager->list);
    
    // 初始化红黑树
    rb_tree_init(&manager->tree, vmap_area_compare);
    
    // 设置地址范围
    manager->start = start;
    manager->end = end;
    
    // 创建初始空闲区域
    vmap_area_t *initial_area = (vmap_area_t *)kmalloc(sizeof(vmap_area_t));
    if (initial_area) {
        initial_area->va_start = start;
        initial_area->va_end = end;
        
        // 初始化节点
        INIT_LIST_HEAD(&initial_area->list);
        
        // 添加到链表
        list_add(&initial_area->list, &manager->list);
        
        // 添加到红黑树
        uint64_t key = initial_area->va_start;
        rb_tree_insert(&manager->tree, &key, initial_area);
        
        printk("vmap_manager: initialized with initial area 0x%llx-0x%llx\n", start, end);
    } else {
        printk("vmap_manager: failed to allocate initial area\n");
    }
}

// 初始化全局 vmap 管理器
void vmap_init(void) {
    // 初始化全局空闲管理器
    vmap_manager_init(&free_vmap, VMALLOC_START, VMALLOC_START + VMALLOC_SIZE);
    
    // 初始化已分配链表
    INIT_LIST_HEAD(&alloced_vm_list);
    
    printk("vmap: initialized\n");
}

// 从红黑树节点获取 vmap_area 结构体
#define vmap_area_from_rb(node) container_of(node, vmap_area_t, rb_node)

// 从链表节点获取 vmap_area 结构体
#define vmap_area_from_list(node) container_of(node, vmap_area_t, list)

// 从链表节点获取 vm_struct 结构体
#define vm_struct_from_list(node) container_of(node, vm_struct_t, list)

// 分配虚拟地址
vm_struct_t *vmap_alloc(size_t size, uint32_t flags) {
    if (size == 0) {
        return NULL;
    }

    // 对齐到页大小
    size_t aligned_size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    // 查找合适的空闲区域
    // 这里简化实现，实际应该遍历红黑树找到合适的区域
    vmap_area_t *free_area = NULL;
    uint64_t alloc_start = 0;
    
    // 遍历空闲链表查找合适的区域
    list_head_t *pos;
    list_for_each(pos, &free_vmap.list) {
        vmap_area_t *area = vmap_area_from_list(pos);
        uint64_t area_size = area->va_end - area->va_start;
        
        if (area_size >= aligned_size) {
            free_area = area;
            alloc_start = area->va_start;
            break;
        }
    }
    
    if (!free_area) {
        printk("vmap_alloc: no available virtual address space for size %zu\n", size);
        return NULL;
    }
    
    // 分配 vm_struct 结构体
    vm_struct_t *vm = (vm_struct_t *)kmalloc(sizeof(vm_struct_t));
    if (!vm) {
        printk("vmap_alloc: failed to allocate vm_struct\n");
        return NULL;
    }
    
    // 设置虚拟地址范围
    vm->va_start = alloc_start;
    vm->va_end = alloc_start + aligned_size;
    vm->flags = flags;
    vm->phys_pages = NULL;
    vm->nr_pages = aligned_size >> PAGE_SHIFT;
    vm->ioremap_phys = 0;
    
    // 初始化链表节点
    INIT_LIST_HEAD(&vm->list);
    
    // 将 vm_struct 添加到已分配链表
    list_add(&vm->list, &alloced_vm_list);
    
    // 更新空闲区域
    if (free_area->va_end == vm->va_end) {
        // 整个空闲区域被分配，从链表和红黑树中移除
        list_del(&free_area->list);
        kfree(free_area);
    } else {
        // 分割空闲区域
        free_area->va_start = vm->va_end;
    }
    
    printk("vmap_alloc: allocated 0x%zu bytes at 0x%llx-0x%llx\n", 
           size, vm->va_start, vm->va_end);
    
    return vm;
}

// 释放虚拟地址
void vmap_free(vm_struct_t *vm) {
    if (!vm) {
        return;
    }
    
    // 从已分配链表中移除
    list_del(&vm->list);
    
    // 创建新的空闲区域
    vmap_area_t *new_free_area = (vmap_area_t *)kmalloc(sizeof(vmap_area_t));
    if (new_free_area) {
        new_free_area->va_start = vm->va_start;
        new_free_area->va_end = vm->va_end;
        
        // 初始化链表节点
        INIT_LIST_HEAD(&new_free_area->list);
        
        // 添加到空闲链表
        list_add(&new_free_area->list, &free_vmap.list);
        
        // 添加到红黑树
        uint64_t key = new_free_area->va_start;
        rb_tree_insert(&free_vmap.tree, &key, new_free_area);
    }
    
    // 释放相关资源
    if (vm->phys_pages) {
        kfree(vm->phys_pages);
    }
    
    kfree(vm);
    
    printk("vmap_free: freed virtual address 0x%llx-0x%llx\n", 
           vm->va_start, vm->va_end);
}
