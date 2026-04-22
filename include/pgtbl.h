#ifndef _PGTBL_H
#define _PGTBL_H

#include <types.h>

/* =========================================================
 * ARM64 4KB granule, 4-level page table
 * ===================  ======================================
 *
 * VA[47:39] -> L0 (PGD)
 * VA[38:30] -> L1 (PUD)
 * VA[29:21] -> L2 (PMD)
 * VA[20:12] -> L3 (PTE)
 * VA[11:0]  -> page offset
 */

#define PTRS_PER_PTE 512UL
#define ARM64_PTE_VALID (1UL << 0)
#define ARM64_PTE_TYPE_BLOCK (0UL << 1)
#define ARM64_PTE_TYPE_TABLE (1UL << 1)
#define ARM64_PTE_TYPE_PAGE (1UL << 1)

/*
 * 页表项属性位
 */
#define ARM64_PTE_AF (1UL << 10)
#define ARM64_PTE_SH_INNER (3UL << 8)
#define ARM64_PTE_AP_RW_KERNEL (0UL << 6)
#define ARM64_PTE_ATTRIDX(idx) ((unsigned long)(idx) << 2)
#define ARM64_PTE_UXN (1UL << 54)
#define ARM64_PTE_PXN (1UL << 53)

/*
 * 默认按 AttrIndx=0 映射普通内存
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

/* ===============================================
 * 🔔 重要配置 - 必须在 pgtbl_init() 之前设置！ 🔔
 * ===============================================
 *
 * linear_map_base:
 *   线性映射区基址，必须满足:
 *     VA = linear_map_base + PA
 *   用于将物理地址转换为虚拟地址
 *
 * l0_table_pa:
 *   ARM64 四级页表的 L0 表物理地址
 *   用于页表操作和地址映射
 * =============================================== */
extern uintptr_t linear_map_base;
extern phys_addr_t l0_table_pa;

static inline void *pgtbl_phys_to_virt(phys_addr_t pa) {
  return (void *)(linear_map_base + (uintptr_t)pa);
}

static inline phys_addr_t pgtbl_virt_to_phys(const void *va) {
  return (phys_addr_t)((uintptr_t)va - linear_map_base);
}

static inline bool pgtbl_va_in_linear_map(const void *va) {
  uintptr_t v = (uintptr_t)va;

  return v >= linear_map_base;
}

static inline phys_addr_t pte_to_phys(pte_t pte) {
    return (phys_addr_t)(pte & ARM64_PTE_ADDR_MASK);
}

static inline int pte_table_empty(pte_t *table) {
    for (uint32_t i = 0; i < PTRS_PER_PTE; i++) {
        if (table[i])
            return 0;
    }
    return 1;
}

static inline bool pte_present(pte_t pte) { return !!(pte & ARM64_PTE_VALID); }

/* 页表硬件操作函数 */
int pgtbl_map_one_page(uintptr_t va, phys_addr_t pa, uint64_t prot);
void pgtbl_unmap_one_page(uintptr_t va);
int pgtbl_map_range(uintptr_t va, phys_addr_t pa, size_t size, uint64_t prot);
void pgtbl_unmap_range(uintptr_t va, size_t size);

/* 内存属性和寄存器操作 */
void pgtbl_set_mair_el1(void);
void pgtbl_set_tcr_el1(void);
void pgtbl_set_sctlr_el1(void);
void pgtbl_switch_ttbr0(phys_addr_t ttbr0_pa);
void pgtbl_flush_tlb(void);

#endif /* _PGTBL_H */