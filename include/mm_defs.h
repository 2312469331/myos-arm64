#ifndef __MM_DEFS_H__
#define __MM_DEFS_H__

#include <types.h>
#include <ds/list.h>


// 基本数据类型定义
typedef unsigned long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef unsigned long phys_addr_t;
typedef unsigned long size_t;

// 地址空间布局宏定义
/* 可修改的物理内存管理范围 */
#define BUDDY_MEM_START 0x4058b000
#define BUDDY_MEM_END 0x4FFFFFFFUL
/* 可修改的虚拟内存管理范围 */
#define VIRT_START      0xffff000000000000UL  // 用户空间起始地址
#define USER_SPACE_SIZE       (0x8000000000UL)  // 用户空间大小：512GB
#define KERNEL_SPACE_START   (0xffff800000000000UL)  // 内核线性映射区起始地址
#define KERNEL_SPACE_SIZE    (0x8000000000UL)  // 内核线性映射区大小：512GB
#define VMALLOC_START        VIRT_START // 虚拟内存分配区起始地址
#define VMALLOC_SIZE         (0x1000000000UL)  // 虚拟内存分配区大小：64GB
#define PAGE_SIZE            (4096UL)  // 页大小：4KB
#define PAGE_SHIFT           (12)  // 页大小移位
#define PAGE_MASK            (~(PAGE_SIZE - 1UL))  // 页掩码

// 权限宏定义
#define PROT_READ            (1 << 0)  // 可读
#define PROT_WRITE           (1 << 1)  // 可写
#define PROT_EXEC            (1 << 2)  // 可执行
#define MAP_ANONYMOUS        (1 << 3)  // 匿名映射
#define MAP_FILE             (1 << 4)  // 文件映射

// 物理页状态
#define PAGE_FREE            (0)  // 空闲
#define PAGE_ALLOCATED       (1)  // 已分配
#define PAGE_RESERVED        (2)  // 保留

// 物理页描述符
struct page {
    u16 order;  // 当前块的阶，仅对块首页有意义
    u16 private;  // 预留字段
    u32 flags;  // 状态位
    struct list_head node;  // 挂入 free_area[order] 的链表节点
    uint64_t ref_count;  // 引用计数
    void *virtual_address;  // 虚拟地址（如果已映射）
};



// struct vmap_area {
//     unsigned long va_start;
//     unsigned long va_end;
//     struct rb_node rb_node;   // 红黑树，快速找空闲虚拟地址
//     struct vm_struct *vm;     // 指向上面的 vm_struct
// };

#include <sync/spinlock.h>


// 内存管理结构
struct mm_struct {
    uint64_t start_code;  // 代码段起始
    uint64_t end_code;  // 代码段结束
    uint64_t start_data;  // 数据段起始
    uint64_t end_data;  // 数据段结束
    uint64_t start_brk;  // 堆起始
    uint64_t brk;  // 堆当前位置
    uint64_t start_stack;  // 栈起始
    struct vma_area *mmap;  // 虚拟内存区域链表
    struct page *page_table;  // 页表
    spinlock_t page_table_lock;  // 页表锁
};

/* =========================================================
 * 内核地址空间
 * =========================================================
 * init_mm 是内核唯一的地址空间，用于管理内核的虚拟内存
 * 包括内核代码段、数据段、vmalloc 区域等
 */
extern struct mm_struct init_mm;

#endif /* __MM_DEFS_H__ */
