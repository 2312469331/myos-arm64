#include <pgtbl.h>
#include <pmm.h>
#include <mmu.h>
uintptr_t linear_map_base;
phys_addr_t l0_table_pa;

/* 映射单个页面 */
int pgtbl_map_one_page(uintptr_t va, phys_addr_t pa, uint64_t prot) {
    // 设置 pgtbl 所需的全局变量
    linear_map_base = slab_linear_map_base;
    l0_table_pa = slab_l0_table_pa;
    return arm64_map_one_page(va, pa, prot);
}

void pgtbl_unmap_one_page(uintptr_t va) {
      // 设置 pgtbl 所需的全局变量
    linear_map_base = slab_linear_map_base;
    l0_table_pa = slab_l0_table_pa;
    
    arm64_unmap_one_page(va);
}

int pgtbl_map_range(uintptr_t va, phys_addr_t pa, size_t size, uint64_t prot) {
  size_t off, len;

  len = ((size + 4095) & ~4095);
  for (off = 0; off < len; off += 4096) {
    if (pgtbl_map_one_page(va + off, pa + off, prot))
      return -1;
  }
  return 0;
}

void pgtbl_unmap_range(uintptr_t va, size_t size) {
  size_t off, len;

  len = ((size + 4095) & ~4095);
  for (off = 0; off < len; off += 4096)
    pgtbl_unmap_one_page(va + off);
}



/* ============================================================
 * 9. 内核页表操作辅助函数
 * ============================================================
 */

/**
 * map_kernel_page - 映射内核页表
 * @va: 虚拟地址
 * @pa: 物理地址
 * @prot: 页表属性
 */
void map_kernel_page(uint64_t va, uint64_t pa, uint64_t prot) {
    // 使用pgtbl_map_one_page映射页表
    pgtbl_map_one_page((uintptr_t)va, (phys_addr_t)pa, prot);
}

/**
 * unmap_kernel_page - 解除内核页表映射
 * @va: 虚拟地址
 */
void unmap_kernel_page(uint64_t va) {
    // 使用pgtbl_unmap_one_page解除映射
    pgtbl_unmap_one_page((uintptr_t)va);
}
