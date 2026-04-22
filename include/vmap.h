#ifndef _VMAP_H
#define _VMAP_H

#include <types.h>
#include <ds/list.h>
#include <ds/rbtree.h>
#include <sync/spinlock.h>

#define VA_ALIGN_DEFAULT 0x1000UL

#define VA_FLAG_VMALLOC    (1UL << 0)
#define VA_FLAG_VMAP       (1UL << 1)
#define VA_FLAG_IOREMAP    (1UL << 2)

struct vmap_area {
    uint64_t va_start;
    uint64_t va_end;
    unsigned long flags;
    // --- 新增：专门用于“忙碌状态”的物理页管理 ---
    struct page **pages;     // 指向一个数组，记录了这段虚拟地址绑定的物理页
    unsigned int nr_pages;   // 记录有几个物理页
    // ------------------------------------------
    list_head_t free_list;
    struct rb_node free_rb_node;
    list_head_t busy_list;
};

struct vmap_manager {
    uint64_t start;
    uint64_t end;
    
    rb_tree_t free_tree;
    struct list_head free_list;
    struct list_head busy_list;
    spinlock_t lock;
    uint64_t total_free;
    uint64_t nr_busy;
};

void va_manager_init(void);
void *va_alloc(unsigned long size, unsigned long align, unsigned long flags);
void va_free(void *addr);

#endif