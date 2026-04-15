#ifndef _SLAB_H
#define _SLAB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* =========================================================
 * 基本常量
 * =========================================================
 */

#define PAGE_SHIFT 12UL
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define PAGE_MASK (~(PAGE_SIZE - 1))

#define SZ_8K (8UL * 1024)
#define SZ_4M (4UL * 1024 * 1024)

#define SLAB_CACHE_COUNT 11

/*
 * 纯基础实现，元数据池固定大小。
 * 如果你觉得不够，可以调大。
 */
#define MAX_SLAB_PAGES 4096
#define MAX_LARGE_ALLOCS 2048

#define SLAB_MAGIC 0x534C4142UL  /* "SLAB" */
#define LARGE_MAGIC 0x4C415247UL /* "LARG" */

/* =========================================================
 * ARM64 4KB granule, 4-level page table
 * =========================================================
 *
 * VA[47:39] -> L0
 * VA[38:30] -> L1
 * VA[29:21] -> L2
 * VA[20:12] -> L3
 * VA[11:0]  -> page offset
 */

#define PTRS_PER_PTE 512UL
#define ARM64_PTE_VALID (1UL << 0)
#define ARM64_PTE_TYPE_BLOCK (0UL << 1)
#define ARM64_PTE_TYPE_TABLE (1UL << 1)
#define ARM64_PTE_TYPE_PAGE (1UL << 1)

/*
 * 下面这些属性位需要你按自己内核的 MAIR/TCR 配置确认。
 * 这里给出一版最常见的 normal memory inner shareable kernel RW 模板。
 */
#define ARM64_PTE_AF (1UL << 10)
#define ARM64_PTE_SH_INNER (3UL << 8)
#define ARM64_PTE_AP_RW_KERNEL (0UL << 6)
#define ARM64_PTE_ATTRIDX(idx) ((unsigned long)(idx) << 2)
#define ARM64_PTE_UXN (1UL << 54)
#define ARM64_PTE_PXN (1UL << 53)

/*
 * 默认按 AttrIndx=0 映射普通内存。
 * 若你的 MAIR[0] 不是 normal memory，请修改。
 */
#define ARM64_PAGE_PROT                                                        \
  (ARM64_PTE_VALID | ARM64_PTE_TYPE_PAGE | ARM64_PTE_AF | ARM64_PTE_SH_INNER | \
   ARM64_PTE_AP_RW_KERNEL | ARM64_PTE_ATTRIDX(0) | ARM64_PTE_UXN |             \
   ARM64_PTE_PXN)

#define ARM64_TABLE_PROT (ARM64_PTE_VALID | ARM64_PTE_TYPE_TABLE)

#define ARM64_PTE_ADDR_MASK 0x0000FFFFFFFFF000UL

#define L0_INDEX(va) (((unsigned long)(va) >> 39) & 0x1FFUL)
#define L1_INDEX(va) (((unsigned long)(va) >> 30) & 0x1FFUL)
#define L2_INDEX(va) (((unsigned long)(va) >> 21) & 0x1FFUL)
#define L3_INDEX(va) (((unsigned long)(va) >> 12) & 0x1FFUL)

typedef uint64_t pte_t;
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
