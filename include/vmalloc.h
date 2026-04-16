#ifndef _VMALLOC_H
#define _VMALLOC_H

#include <types.h>
#include <mm_defs.h>
#include <ds/list.h>

/* =========================================================
 * vmalloc 区域定义
 * =========================================================
 * 内核虚拟内存分配区，位于高地址空间
 * 仅内核可访问，用于动态分配虚拟内存
 */

#define VMALLOC_START        (KERNEL_SPACE_START + 0x2000000000UL)
#define VMALLOC_END          (VMALLOC_START + VMALLOC_SIZE - 1UL)
#define VMALLOC_SIZE         (0x1000000000UL)

/* =========================================================
 * vmalloc 区域管理结构
 * =========================================================
 */

struct vmalloc_area {
    uint64_t start;          // 起始虚拟地址
    uint64_t size;           // 大小（字节）
    uint64_t flags;          // 标志位
    phys_addr_t *pages;      // 物理页数组
    unsigned int nr_pages;   // 物理页数量
    struct list_head list;    // 链表节点
    const char *caller;      // 调用者信息
};

/* =========================================================
 * vmalloc 标志位
 * =========================================================
 */
#define VMALLOC_MAP          (1 << 0)  // 立即映射
#define VMALLOC_LAZY         (1 << 1)  // 延迟映射（缺页时映射）
#define VMALLOC_EXEC          (1 << 2)  // 可执行

/* =========================================================
 * vmalloc 核心接口
 * =========================================================
 */

void *vmalloc(size_t size);
void vfree(const void *addr);
void *vmalloc_lazy(size_t size);

/* =========================================================
 * vmalloc 初始化和管理
 * =========================================================
 */

void vmalloc_init(void);
void vmalloc_area_init(struct vmalloc_area *area, uint64_t start, 
                      size_t size, uint64_t flags);

/* =========================================================
 * vmalloc 查询和调试
 * =========================================================
 */

int vmalloc_get_info(const void *addr, struct vmalloc_area *info);
void vmalloc_print_areas(void);

/* =========================================================
 * 内部接口（供 page_fault 使用）
 * =========================================================
 */

spinlock_t *get_vmalloc_lock(void);
struct vmalloc_area *find_vmalloc_area_unlocked(uint64_t addr);

#endif /* _VMALLOC_H */
