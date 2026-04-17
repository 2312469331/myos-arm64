#ifndef __VMA_H__
#define __VMA_H__

#include <types.h>
#include <mm_defs.h>
#include <ds/list.h>
#include <ds/rbtree.h>
// 空闲虚拟地址块（只存空闲，红黑树+链表）
typedef struct vmap_area {
    list_head_t    list;
    rb_node_t      rb_node;
    uint64_t       va_start;
    uint64_t       va_end;
} vmap_area_t;

// 全局空闲管理器
typedef struct vmap_manager {
    rb_tree_t      tree;   // 空闲红黑树
    list_head_t    list;   // 空闲链表
    uint64_t       start;
    uint64_t       end;
} vmap_manager_t;
vmap_manager_t free_vmap; // 全局唯一

// 🔥 已分配虚拟地址记录体（vmalloc/ioremap 共用）
// 释放时，全靠它找地址、回收资源
typedef struct vm_struct {
    list_head_t    list;       // 挂全局已分配链表
    uint64_t       va_start;   // 分配的虚拟地址（核心）
    uint64_t       va_end;     // 结束地址（核心）
    uint32_t       flags;      // 区分类型：VM_ALLOC(vmalloc) / VM_IOREMAP(ioremap)

    // vmalloc 专用：保存物理页地址数组
    uint64_t       *phys_pages;
    uint32_t        nr_pages;

    // ioremap 专用：保存设备物理地址
    uint64_t        ioremap_phys;
} vm_struct_t;

// 全局已分配链表（所有申请的地址，都挂在这里）
list_head_t alloced_vm_list;

// 初始化 vmap_manager
void vmap_manager_init(vmap_manager_t *manager, uint64_t start, uint64_t end);

// 初始化全局 vmap 管理器
void vmap_init(void);

// 分配虚拟地址
// size: 分配大小（字节）
// flags: 标志位（VM_ALLOC 或 VM_IOREMAP）
// 返回值: 分配的虚拟地址范围，失败返回 NULL
vm_struct_t *vmap_alloc(size_t size, uint32_t flags);

// 释放虚拟地址
// vm: 要释放的虚拟地址结构体
void vmap_free(vm_struct_t *vm);

// 容器反查宏
// 从红黑树节点获取 vmap_area 结构体
#define vmap_area_from_rb(node) container_of(node, vmap_area_t, rb_node)

// 从链表节点获取 vmap_area 结构体
#define vmap_area_from_list(node) container_of(node, vmap_area_t, list)

// 从链表节点获取 vm_struct 结构体
#define vm_struct_from_list(node) container_of(node, vm_struct_t, list)

#endif /* __VMA_H__ */