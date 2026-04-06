#ifndef _SLAB_H
#define _SLAB_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef phys_addr_t
typedef uint64_t phys_addr_t;
#endif

/* ===============================================
 * 🔔 重要配置 - 必须在 slab_init() 之前设置！ 🔔
 * ===============================================
 * 
 * slab_linear_map_base:
 *   线性映射区基址，必须满足:
 *     VA = slab_linear_map_base + PA
 *   用于将物理地址转换为虚拟地址
 *
 * slab_l0_table_pa:
 *   ARM64 四级页表的 L0 表物理地址
 *   用于页表操作和地址映射
 * =============================================== */
extern uintptr_t slab_linear_map_base;
extern phys_addr_t slab_l0_table_pa;

/*
 * 固定伙伴系统接口（用户要求）
 */
phys_addr_t alloc_phys_pages(unsigned int order);
void free_phys_pages(phys_addr_t pa, unsigned int order);

/*
 * 初始化全部 slab cache
 */
void slab_init(void);

/*
 * kmalloc / kfree
 */
void *kmalloc(size_t size);
void kfree(void *va);

#endif /* _SLAB_H */