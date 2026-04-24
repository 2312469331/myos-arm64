#ifndef _SLAB_H
#define _SLAB_H

#include <stddef.h>
#include <stdint.h>
#include <types.h>
#include <gfp.h>  // 内核分配标志
/* =========================================================
 * 基本常量
 * =========================================================
 */
#define SZ_8K (8UL * 1024)
#define SZ_4M (4UL * 1024 * 1024)
#define SLAB_CACHE_COUNT 11

/*
 * 纯基础实现，元数据池固定大小。
 * 如果觉得不够，可以调大。
 */
#define MAX_SLAB_PAGES 4096
#define MAX_LARGE_ALLOCS 2048
#define SLAB_MAGIC 0x534C4142UL  /* "SLAB" */
#define LARGE_MAGIC 0x4C415247UL /* "LARG" */

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
 * 初始化全部 slab cache
 */
void slab_init(void);

/*
 * kmalloc / kfree
 */
void *kmalloc(size_t size, gfp_t flags);
void kfree(void *va);

#endif /* _SLAB_H */
