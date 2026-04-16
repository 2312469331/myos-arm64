#include "mmu.h"
#include "types.h"
#include "pmm.h"
uintptr_t slab_linear_map_base;
phys_addr_t slab_l0_table_pa;

/* 映射单个页面 */
int arm64_map_one_page(uintptr_t va, phys_addr_t pa) {
    pte_t *l0, *l1, *l2, *l3;
    phys_addr_t l1_pa, l2_pa, l3_pa;
    unsigned long idx0, idx1, idx2, idx3;

    if (!slab_l0_table_pa)
        return -1;

    l0 = (pte_t *)phys_to_virt(slab_l0_table_pa);

    idx0 = L0_INDEX(va);
    idx1 = L1_INDEX(va);
    idx2 = L2_INDEX(va);
    idx3 = L3_INDEX(va);

    /* 1. 检查/创建 L1 表 */
    if (!pte_present(l0[idx0])) {
        l1_pa = alloc_phys_pages(0);
        if (!l1_pa)
            return -1;
        l0[idx0] = ARM64_TABLE_PROT | l1_pa;
    } else {
        l1_pa = pte_to_phys(l0[idx0]);
    }
    l1 = (pte_t *)phys_to_virt(l1_pa);

    /* 2. 检查/创建 L2 表 */
    if (!pte_present(l1[idx1])) {
        l2_pa = alloc_phys_pages(0);
        if (!l2_pa)
            return -1;
        l1[idx1] = ARM64_TABLE_PROT | l2_pa;
    } else {
        l2_pa = pte_to_phys(l1[idx1]);
    }
    l2 = (pte_t *)phys_to_virt(l2_pa);

    /* 3. 检查/创建 L3 表 */
    if (!pte_present(l2[idx2])) {
        l3_pa = alloc_phys_pages(0);
        if (!l3_pa)
            return -1;
        l2[idx2] = ARM64_TABLE_PROT | l3_pa;
    } else {
        l3_pa = pte_to_phys(l2[idx2]);
    }
    l3 = (pte_t *)phys_to_virt(l3_pa);

    /* 4. 映射页面 */
    l3[idx3] = ARM64_PAGE_PROT | pa;

    return 0;
}

void arm64_unmap_one_page(uintptr_t va) {
  pte_t *l0, *l1, *l2, *l3;
  phys_addr_t l1_pa, l2_pa, l3_pa;
  unsigned long idx0, idx1, idx2, idx3;

  if (!slab_l0_table_pa)
    return;

  l0 = (pte_t *)phys_to_virt(slab_l0_table_pa);

  idx0 = L0_INDEX(va);
  idx1 = L1_INDEX(va);
  idx2 = L2_INDEX(va);
  idx3 = L3_INDEX(va);

  if (!pte_present(l0[idx0]))
    return;

  l1_pa = pte_to_phys(l0[idx0]);
  l1 = (pte_t *)phys_to_virt(l1_pa);

  if (!pte_present(l1[idx1]))
    return;

  l2_pa = pte_to_phys(l1[idx1]);
  l2 = (pte_t *)phys_to_virt(l2_pa);

  if (!pte_present(l2[idx2]))
    return;

  l3_pa = pte_to_phys(l2[idx2]);
  l3 = (pte_t *)phys_to_virt(l3_pa);

  if (!pte_present(l3[idx3]))
    return;

  /* 1. 先让 L3 页项失效 */
  l3[idx3] = 0;

  /* 2. 检查整个 L3 表是否空，空则释放，并让 L2 对应项失效 */
  if (pte_table_empty(l3)) {
    l2[idx2] = 0;
    free_phys_pages(l3_pa, 0);

    /* 3. 检查整个 L2 表是否空，空则释放，并让 L1 对应项失效 */
    if (pte_table_empty(l2)) {
      l1[idx1] = 0;
      free_phys_pages(l2_pa, 0);

      /* 4. 检查整个 L1 表是否空，空则释放，并让 L0 对应项失效 */
      if (pte_table_empty(l1)) {
        l0[idx0] = 0;
        free_phys_pages(l1_pa, 0);
      }
    }
  }
}

int arm64_map_range(uintptr_t va, phys_addr_t pa, size_t size) {
  size_t off, len;

  len = align_up_ul(size, PAGE_SIZE);
  for (off = 0; off < len; off += PAGE_SIZE) {
    if (arm64_map_one_page(va + off, pa + off))
      return -1;
  }
  return 0;
}

void arm64_unmap_range(uintptr_t va, size_t size) {
  size_t off, len;

  len = align_up_ul(size, PAGE_SIZE);
  for (off = 0; off < len; off += PAGE_SIZE)
    arm64_unmap_one_page(va + off);
}