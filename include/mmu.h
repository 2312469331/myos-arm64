#ifndef _MMU_H
#define _MMU_H

#include <types.h>
#include <pgtbl.h>
#include <mm_defs.h>

/* ===============================================
 * 🔔 重要配置 - 必须在 mmu_init() 之前设置！ 🔔
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

static inline void *phys_to_virt(phys_addr_t pa) {
  return (void *)(slab_linear_map_base + (uintptr_t)pa);
}

static inline phys_addr_t virt_to_phys(const void *va) {
  return (phys_addr_t)((uintptr_t)va - slab_linear_map_base);
}

static inline bool va_in_linear_map(const void *va) {
  uintptr_t v = (uintptr_t)va;

  return v >= slab_linear_map_base;
}

/* 页表操作函数 */
static inline int arm64_map_one_page(uintptr_t va, phys_addr_t pa) {
    // 设置 pgtbl 所需的全局变量
    linear_map_base = slab_linear_map_base;
    l0_table_pa = slab_l0_table_pa;
    
    return pgtbl_map_one_page(va, pa);
}

static inline void arm64_unmap_one_page(uintptr_t va) {
    // 设置 pgtbl 所需的全局变量
    linear_map_base = slab_linear_map_base;
    l0_table_pa = slab_l0_table_pa;
    
    pgtbl_unmap_one_page(va);
}

static inline int arm64_map_range(uintptr_t va, phys_addr_t pa, size_t size) {
    // 设置 pgtbl 所需的全局变量
    linear_map_base = slab_linear_map_base;
    l0_table_pa = slab_l0_table_pa;
    
    return pgtbl_map_range(va, pa, size);
}

static inline void arm64_unmap_range(uintptr_t va, size_t size) {
    // 设置 pgtbl 所需的全局变量
    linear_map_base = slab_linear_map_base;
    l0_table_pa = slab_l0_table_pa;
    
    pgtbl_unmap_range(va, size);
}

/* =========================================================
 * 基础工具
 * =========================================================
 */

static inline unsigned long align_up_ul(unsigned long x, unsigned long a) {
  return (x + a - 1) & ~(a - 1);
}

static inline unsigned int ilog2_ul(unsigned long x) {
  unsigned int r = 0;

  while (x > 1) {
    x >>= 1;
    r++;
  }
  return r;
}

static inline unsigned int get_order_ul(unsigned long size) {
  unsigned long pages;
  unsigned long n = 1;
  unsigned int order = 0;

  if (!size)
    return 0;

  pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
  while (n < pages) {
    n <<= 1;
    order++;
  }
  return order;
}

#endif /* _MMU_H */
