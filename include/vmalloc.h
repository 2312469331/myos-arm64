#ifndef __VMALLOC_H__
#define __VMALLOC_H__

#include <mm_defs.h> // 引入 u64, size_t, phys_addr_t 等
#include <ds/rbtree.h>

// ---------------------------------------------------------
// 虚拟内存空间布局配置 (基于 ARM64 64位内核布局)
// ---------------------------------------------------------
// 假设 vmalloc 区域在线性映射区之上
#define VMALLOC_END          (VMALLOC_START + (VMALLOC_SIZE) - 1) // 虚拟内存分配区结束地址

// 与你的 buddy.h 保持一致，最大 Order 为 10
#define MAX_VMALLOC_ORDER 10
// ---------------------------------------------------------
// 安全补全手册中未列出但必需的链表宏
// ---------------------------------------------------------
#ifndef list_first_entry
#define list_first_entry(ptr, type, member) \
    container_of((ptr)->next, type, member)
#endif

// ---------------------------------------------------------
// 核心数据结构
// ---------------------------------------------------------

// 虚拟内存区域描述符 (对应 Linux 的 vmap_area)
typedef struct vmap_area {
    u64 va_start;          // 虚拟地址起始
    u64 va_end;            // 虚拟地址结束
    int is_free;           // 是否空闲: 1-空闲, 0-已分配
    phys_addr_t phys_start;// 绑定的物理内存起始地址
    int flags;             // 权限标志

    // 挂入全局红黑树 (按 va_start 排序)
    rb_node_t rb_node;
    // 挂入分桶双向链表 (仅空闲时使用)
    list_head_t list_node;
} vmap_area_t;

// Linux 6.x 风格：空闲块分桶结构 (按 Order 大小分桶)
typedef struct vmalloc_bucket {
    spinlock_t lock;        // 【核心】每个桶独立的细粒度自旋锁
    list_head_t free_list;  // 该大小等级的空闲 VMA 链表
} vmalloc_bucket_t;
// ---------------------------------------------------------
// 对外 API
// ---------------------------------------------------------

/**
 * @brief 初始化 vmalloc 子系统
 */
void vmalloc_init(void);
#define vmap_init vmalloc_init

/**
 * @brief 分配连续的虚拟内存，并自动关联物理页
 * @param size 需要的字节数
 * @param flags 权限标志 (如 PROT_READ | PROT_WRITE)
 * @return 返回内核虚拟地址，失败返回 NULL
 */
void *vmalloc(size_t size, int flags);

/**
 * @brief 释放虚拟内存，并交还关联的物理页
 * @param addr 要释放的虚拟地址
 */
void vfree(void *addr);

/**
 * @brief 打印当前 vmalloc 空间的布局 (用于调试)
 */
void vmalloc_print_layout(void);

#endif // __VMALLOC_H__
