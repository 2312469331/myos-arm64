/**
 * @file vmm.c
 * @brief 虚拟内存管理器实现
 *
 * 实现虚拟地址空间管理，包括页表操作、地址映射、
 * VMA管理等功能。
 */

#include "vmm.h"
#include "io.h"
#include "kheap.h"
#include "pmm.h"

/*============================================================================
 *                              全局变量
 *============================================================================*/

/* 内核页表 (来自启动代码) */
extern pgt_t ttbr1_l0;

/* 当前页表 */
static pgt_t current_pgt = NULL;

/* 内核内存描述符 */
static mm_struct_t kernel_mm = {0};

/* VMM初始化标志 */
static bool vmm_initialized = false;

/* MMIO映射区域管理 */
static struct {
  virt_addr_t current; /* 当前MMIO映射位置 */
  volatile uint32_t lock;
} mmio_mapper = {.current = VMM_KERNEL_MMIO_START, .lock = 0};

/*============================================================================
 *                              内联辅助函数
 *============================================================================*/

/**
 * @brief 获取自旋锁
 */
static inline void vmm_lock(volatile uint32_t *lock) {
  while (__atomic_test_and_set(lock, __ATOMIC_ACQUIRE)) {
    while (*lock) {
      __asm__ volatile("yield" ::: "memory");
    }
  }
}

/**
 * @brief 释放自旋锁
 */
static inline void vmm_unlock(volatile uint32_t *lock) {
  __atomic_clear(lock, __ATOMIC_RELEASE);
}

/**
 * @brief 分配一个页表
 */
static inline pgt_t alloc_page_table(void) {
  phys_addr_t pa = pmm_alloc_page(PMM_FLAG_ZERO);
  if (pa == 0) {
    return NULL;
  }
  return (pgt_t)PHYS_TO_VIRT(pa);
}

/**
 * @brief 获取页表项指向的下一级页表
 */
static inline pgt_t pte_to_table(pte_t pte) {
  if (!(pte & PTE_VALID) || !(pte & PTE_TABLE)) {
    return NULL;
  }
  phys_addr_t pa = pte & PTE_ADDR_MASK;
  return (pgt_t)PHYS_TO_VIRT(pa);
}

/*============================================================================
 *                              初始化函数
 *============================================================================*/

/**
 * @brief 初始化虚拟内存管理器
 */
int vmm_init(void) {
  if (vmm_initialized) {
    return 0;
  }

  /* 使用启动代码创建的内核页表 */
  current_pgt = (pgt_t)PHYS_TO_VIRT((phys_addr_t)ttbr1_l0);

  /* 初始化内核内存描述符 */
  kernel_mm.pgd = current_pgt;
  kernel_mm.start_code = VMM_KERNEL_CODE_START;
  kernel_mm.end_code = VMM_KERNEL_CODE_END;
  kernel_mm.start_data = VMM_KERNEL_DATA_START;
  kernel_mm.end_data = VMM_KERNEL_DATA_END;
  kernel_mm.start_brk = VMM_KERNEL_HEAP_START;
  kernel_mm.brk = VMM_KERNEL_HEAP_START;
  kernel_mm.mmap = NULL;
  kernel_mm.map_count = 0;
  kernel_mm.total_vm = 0;
  kernel_mm.lock = 0;
  kernel_mm.ref_count = 1;

  vmm_initialized = true;

  return 0;
}

/**
 * @brief 初始化内核页表
 */
int vmm_init_kernel_pgt(void) {
  /* 内核页表已在启动代码中初始化 */
  return 0;
}

/*============================================================================
 *                              页表操作函数
 *============================================================================*/

/**
 * @brief 创建新的页表
 */
pgt_t vmm_create_pgt(void) {
  pgt_t pgt = alloc_page_table();
  if (!pgt) {
    return NULL;
  }

  /* 清空页表 */
  memzero(pgt, PAGE_SIZE);

  /* 复制内核映射 */
  if (vmm_map_kernel_space(pgt) < 0) {
    pmm_free_page(VIRT_TO_PHYS(pgt));
    return NULL;
  }

  return pgt;
}

/**
 * @brief 销毁页表
 */
void vmm_destroy_pgt(pgt_t pgt) {
  if (!pgt) {
    return;
  }

  /* 注意: 这里需要递归释放所有子页表 */
  /* 简化实现: 只释放L0表 */
  pmm_free_page(VIRT_TO_PHYS(pgt));
}

/**
 * @brief 切换当前页表
 */
void vmm_switch_pgt(pgt_t pgt) {
  if (!pgt) {
    return;
  }

  phys_addr_t pa = VIRT_TO_PHYS(pgt);

  /* 设置TTBR1_EL1 */
  __asm__ volatile("msr TTBR1_EL1, %0\n"
                   "isb\n" ::"r"(pa));

  /* 刷新TLB */
  vmm_invalidate_tlb_all();

  current_pgt = pgt;
}

/**
 * @brief 获取当前页表
 */
pgt_t vmm_get_current_pgt(void) { return current_pgt; }

/**
 * @brief 遍历页表获取PTE
 */
pte_t *vmm_walk_pt(virt_addr_t va, pte_walker_t *walker, bool alloc) {
  if (!current_pgt || !walker) {
    return NULL;
  }

  /* 清空walker */
  memzero(walker, sizeof(pte_walker_t));

  /* L0 表 */
  uint64_t l0_idx = VA_L0_INDEX(va);
  walker->l0_entry = &current_pgt[l0_idx];

  pte_t l0_pte = *walker->l0_entry;

  /* 检查L0表项 */
  if (!(l0_pte & PTE_VALID)) {
    if (!alloc) {
      return NULL;
    }
    /* 分配L1表 */
    walker->l1_table = alloc_page_table();
    if (!walker->l1_table) {
      return NULL;
    }

    /* 设置L0表项 */
    *walker->l0_entry = PTE_VALID | PTE_TABLE | VIRT_TO_PHYS(walker->l1_table);
  } else {
    walker->l1_table = pte_to_table(l0_pte);
  }

  /* L1 表 */
  uint64_t l1_idx = VA_L1_INDEX(va);
  walker->l1_entry = &walker->l1_table[l1_idx];

  pte_t l1_pte = *walker->l1_entry;

  /* 检查L1表项 */
  if (!(l1_pte & PTE_VALID)) {
    if (!alloc) {
      return NULL;
    }
    /* 分配L2表 */
    walker->l2_table = alloc_page_table();
    if (!walker->l2_table) {
      return NULL;
    }

    /* 设置L1表项 */
    *walker->l1_entry = PTE_VALID | PTE_TABLE | VIRT_TO_PHYS(walker->l2_table);
  } else {
    walker->l2_table = pte_to_table(l1_pte);
  }

  /* L2 表 */
  uint64_t l2_idx = VA_L2_INDEX(va);
  walker->l2_entry = &walker->l2_table[l2_idx];

  pte_t l2_pte = *walker->l2_entry;

  /* 检查L2表项 */
  if (!(l2_pte & PTE_VALID)) {
    if (!alloc) {
      return NULL;
    }
    /* 分配L3表 */
    walker->l3_table = alloc_page_table();
    if (!walker->l3_table) {
      return NULL;
    }

    /* 设置L2表项 */
    *walker->l2_entry = PTE_VALID | PTE_TABLE | VIRT_TO_PHYS(walker->l3_table);
  } else {
    walker->l3_table = pte_to_table(l2_pte);
  }

  /* L3 表 */
  uint64_t l3_idx = VA_L3_INDEX(va);
  walker->l3_entry = &walker->l3_table[l3_idx];

  return walker->l3_entry;
}

/**
 * @brief 获取虚拟地址的PTE
 */
pte_t *vmm_get_pte(pgt_t pgt, virt_addr_t va, bool alloc) {
  if (!pgt) {
    return NULL;
  }

  /* 临时切换页表 */
  pgt_t old_pgt = current_pgt;
  current_pgt = pgt;

  pte_walker_t walker;
  pte_t *pte = vmm_walk_pt(va, &walker, alloc);

  current_pgt = old_pgt;
  return pte;
}

/*============================================================================
 *                              地址映射函数
 *============================================================================*/

/**
 * @brief 映射单个页面
 */
int vmm_map_page(pgt_t pgt, virt_addr_t va, phys_addr_t pa, uint64_t flags) {
  if (!pgt || !IS_PAGE_ALIGNED(va) || !IS_PAGE_ALIGNED(pa)) {
    return -1;
  }

  pte_walker_t walker;
  pte_t *pte = vmm_walk_pt(va, &walker, true);
  if (!pte) {
    return -1;
  }

  /* 设置页表项 */
  *pte = flags | pa;

  /* 刷新TLB */
  vmm_invalidate_tlb(va);

  return 0;
}

/**
 * @brief 映射页面范围
 */
int vmm_map_range(pgt_t pgt, virt_addr_t va_start, phys_addr_t pa_start,
                  uint64_t size, uint64_t flags) {
  if (!pgt || size == 0) {
    return -1;
  }

  /* 页对齐 */
  va_start = PAGE_ALIGN(va_start);
  pa_start = PAGE_ALIGN(pa_start);
  size = PAGE_ALIGN(size);

  uint64_t pages = size / PAGE_SIZE;

  for (uint64_t i = 0; i < pages; i++) {
    virt_addr_t va = va_start + i * PAGE_SIZE;
    phys_addr_t pa = pa_start + i * PAGE_SIZE;

    if (vmm_map_page(pgt, va, pa, flags) < 0) {
      /* 回滚已映射的页面 */
      for (uint64_t j = 0; j < i; j++) {
        vmm_unmap_page(pgt, va_start + j * PAGE_SIZE);
      }
      return -1;
    }
  }

  return 0;
}

/**
 * @brief 映射大页 (2MB)
 */
int vmm_map_huge_page(pgt_t pgt, virt_addr_t va, phys_addr_t pa,
                      uint64_t flags) {
  if (!pgt) {
    return -1;
  }

  /* 检查2MB对齐 */
  if ((va & (PAGE_SIZE_2M - 1)) || (pa & (PAGE_SIZE_2M - 1))) {
    return -1;
  }

  /* 获取L2表项 */
  pte_walker_t walker;
  pte_t *l2_entry = vmm_walk_pt(va, &walker, true);
  if (!l2_entry) {
    return -1;
  }

  /* 设置L2块描述符 */
  *walker.l2_entry = flags | pa | PTE_BLOCK;

  vmm_invalidate_tlb(va);

  return 0;
}

/**
 * @brief 取消映射页面
 */
int vmm_unmap_page(pgt_t pgt, virt_addr_t va) {
  if (!pgt || !IS_PAGE_ALIGNED(va)) {
    return -1;
  }

  pte_walker_t walker;
  pte_t *pte = vmm_walk_pt(va, &walker, false);
  if (!pte || !(*pte & PTE_VALID)) {
    return -1;
  }

  /* 清除页表项 */
  *pte = 0;

  /* 刷新TLB */
  vmm_invalidate_tlb(va);

  return 0;
}

/**
 * @brief 取消映射页面范围
 */
int vmm_unmap_range(pgt_t pgt, virt_addr_t va_start, uint64_t size) {
  if (!pgt || size == 0) {
    return -1;
  }

  va_start = PAGE_ALIGN(va_start);
  size = PAGE_ALIGN(size);

  uint64_t pages = size / PAGE_SIZE;

  for (uint64_t i = 0; i < pages; i++) {
    vmm_unmap_page(pgt, va_start + i * PAGE_SIZE);
  }

  return 0;
}

/**
 * @brief 修改页面属性
 */
int vmm_protect_page(pgt_t pgt, virt_addr_t va, uint64_t flags) {
  if (!pgt || !IS_PAGE_ALIGNED(va)) {
    return -1;
  }

  pte_walker_t walker;
  pte_t *pte = vmm_walk_pt(va, &walker, false);
  if (!pte || !(*pte & PTE_VALID)) {
    return -1;
  }

  /* 保留物理地址，更新属性 */
  phys_addr_t pa = *pte & PTE_ADDR_MASK;
  *pte = flags | pa;

  vmm_invalidate_tlb(va);

  return 0;
}

/**
 * @brief 查询虚拟地址映射
 */
int vmm_query_mapping(pgt_t pgt, virt_addr_t va, phys_addr_t *pa_out,
                      uint64_t *flags_out) {
  if (!pgt) {
    return -1;
  }

  pte_walker_t walker;
  pte_t *pte = vmm_walk_pt(va, &walker, false);
  if (!pte || !(*pte & PTE_VALID)) {
    return -1;
  }

  if (pa_out) {
    *pa_out = *pte & PTE_ADDR_MASK;
  }

  if (flags_out) {
    *flags_out = *pte & ~PTE_ADDR_MASK;
  }

  return 0;
}

/*============================================================================
 *                              VMA管理函数
 *============================================================================*/

/**
 * @brief 创建内存描述符
 */
mm_struct_t *vmm_create_mm(void) {
  mm_struct_t *mm = kzalloc(sizeof(mm_struct_t), GFP_KERNEL);
  if (!mm) {
    return NULL;
  }

  /* 创建页表 */
  mm->pgd = vmm_create_pgt();
  if (!mm->pgd) {
    kfree(mm);
    return NULL;
  }

  mm->mmap = NULL;
  mm->map_count = 0;
  mm->total_vm = 0;
  mm->lock = 0;
  mm->ref_count = 1;

  return mm;
}

/**
 * @brief 销毁内存描述符
 */
void vmm_destroy_mm(mm_struct_t *mm) {
  if (!mm) {
    return;
  }

  /* 释放所有VMA */
  vma_t *vma = mm->mmap;
  while (vma) {
    vma_t *next = vma->vm_next;

    /* 取消映射 */
    vmm_unmap_range(mm->pgd, vma->vm_start, vma->vm_end - vma->vm_start);

    kfree(vma);
    vma = next;
  }

  /* 销毁页表 */
  vmm_destroy_pgt(mm->pgd);

  kfree(mm);
}

/**
 * @brief 创建VMA
 */
vma_t *vmm_create_vma(mm_struct_t *mm, virt_addr_t start, virt_addr_t end,
                      uint64_t flags) {
  if (!mm || start >= end) {
    return NULL;
  }

  /* 页对齐 */
  start = PAGE_ALIGN(start);
  end = PAGE_ALIGN(end);

  vma_t *vma = kzalloc(sizeof(vma_t), GFP_KERNEL);
  if (!vma) {
    return NULL;
  }

  vma->vm_start = start;
  vma->vm_end = end;
  vma->vm_flags = flags;
  vma->vm_mm = mm;
  vma->vm_name = NULL;

  /* 添加到链表 */
  vmm_lock(&mm->lock);

  vma->vm_next = mm->mmap;
  if (mm->mmap) {
    mm->mmap->vm_prev = vma;
  }
  mm->mmap = vma;
  mm->map_count++;
  mm->total_vm += (end - start) / PAGE_SIZE;

  vmm_unlock(&mm->lock);

  return vma;
}

/**
 * @brief 查找包含指定地址的VMA
 */
vma_t *vmm_find_vma(mm_struct_t *mm, virt_addr_t addr) {
  if (!mm) {
    return NULL;
  }

  vma_t *vma = mm->mmap;
  while (vma) {
    if (addr >= vma->vm_start && addr < vma->vm_end) {
      return vma;
    }
    vma = vma->vm_next;
  }

  return NULL;
}

/**
 * @brief 删除VMA
 */
void vmm_remove_vma(mm_struct_t *mm, vma_t *vma) {
  if (!mm || !vma) {
    return;
  }

  vmm_lock(&mm->lock);

  /* 从链表中移除 */
  if (vma->vm_prev) {
    vma->vm_prev->vm_next = vma->vm_next;
  } else {
    mm->mmap = vma->vm_next;
  }

  if (vma->vm_next) {
    vma->vm_next->vm_prev = vma->vm_prev;
  }

  mm->map_count--;
  mm->total_vm -= (vma->vm_end - vma->vm_start) / PAGE_SIZE;

  vmm_unlock(&mm->lock);

  kfree(vma);
}

/**
 * @brief 在VMA范围内映射物理内存
 */
int vmm_map_vma(mm_struct_t *mm, vma_t *vma) {
  if (!mm || !vma) {
    return -1;
  }

  uint64_t size = vma->vm_end - vma->vm_start;
  uint64_t pages = size / PAGE_SIZE;

  /* 分配物理内存 */
  phys_addr_t pa = pmm_alloc_pages(pages, PMM_FLAG_ZERO);
  if (pa == 0) {
    return -1;
  }

  vma->vm_phys = pa;

  /* 映射 */
  uint64_t pte_flags = vma_flags_to_pte(vma->vm_flags);
  return vmm_map_range(mm->pgd, vma->vm_start, pa, size, pte_flags);
}

/*============================================================================
 *                              内核映射函数
 *============================================================================*/

/**
 * @brief 映射内核空间
 */
int vmm_map_kernel_space(pgt_t pgt) {
  if (!pgt) {
    return -1;
  }

  /* 内核代码段: 只读可执行 */
  if (vmm_map_range(pgt, VMM_KERNEL_CODE_START, PHYS_BASE,
                    VMM_KERNEL_CODE_END - VMM_KERNEL_CODE_START,
                    PTE_KERNEL_CODE) < 0) {
    return -1;
  }

  /* 内核数据段: 读写不可执行 */
  if (vmm_map_range(pgt, VMM_KERNEL_DATA_START,
                    PHYS_BASE + (VMM_KERNEL_DATA_START - KERNEL_VIRT_BASE),
                    VMM_KERNEL_DATA_END - VMM_KERNEL_DATA_START,
                    PTE_KERNEL_DATA) < 0) {
    return -1;
  }

  return 0;
}

/**
 * @brief 映射设备MMIO区域
 */
virt_addr_t vmm_map_mmio(phys_addr_t pa, uint64_t size) {
  if (size == 0) {
    return 0;
  }

  /* 对齐 */
  pa = PAGE_ALIGN(pa);
  size = PAGE_ALIGN(size);

  vmm_lock(&mmio_mapper.lock);

  virt_addr_t va = mmio_mapper.current;

  /* 检查是否有足够空间 */
  if (va + size > VMM_KERNEL_MMIO_END) {
    vmm_unlock(&mmio_mapper.lock);
    return 0;
  }

  /* 映射 */
  if (vmm_map_range(current_pgt, va, pa, size, PTE_DEVICE) < 0) {
    vmm_unlock(&mmio_mapper.lock);
    return 0;
  }

  mmio_mapper.current += size;

  vmm_unlock(&mmio_mapper.lock);

  return va;
}

/**
 * @brief 取消映射设备MMIO区域
 */
void vmm_unmap_mmio(virt_addr_t va, uint64_t size) {
  if (va == 0 || size == 0) {
    return;
  }

  vmm_unmap_range(current_pgt, va, size);
}

/**
 * @brief 分配内核虚拟地址空间
 */
virt_addr_t vmm_alloc_kernel_virt(uint64_t size) {
  if (size == 0) {
    return 0;
  }

  size = PAGE_ALIGN(size);

  vmm_lock(&kernel_mm.lock);

  virt_addr_t va = kernel_mm.brk;

  if (va + size > VMM_KERNEL_HEAP_END) {
    vmm_unlock(&kernel_mm.lock);
    return 0;
  }

  kernel_mm.brk += size;

  vmm_unlock(&kernel_mm.lock);

  return va;
}

/*============================================================================
 *                              TLB操作函数
 *============================================================================*/

/**
 * @brief 使TLB条目失效（通用版）
 * @note 基于手册：TLBI VAE1IS - Invalidate translation for EL1
 */
void vmm_invalidate_tlb(virt_addr_t va) {
  __asm__ volatile("dsb sy\n"
                   "tlbi VAE1IS, %0\n" // ✅ 官方标准指令
                   "dsb sy\n"
                   "isb\n" ::"r"(va >> PAGE_SHIFT));
}

/**
 * @brief 使整个TLB失效
 * @note 基于手册：TLBI VMALLE1IS - Invalidate all stage 1 translations
 */
void vmm_invalidate_tlb_all(void) {
  __asm__ volatile("dsb sy\n"
                   "tlbi VMALLE1IS\n" // ✅ 官方标准指令
                   "dsb sy\n"
                   "isb\n");
}

/**
 * @brief 使指令TLB失效（通用用法）
 * @note 架构没有单独ITLB指令，用VAAE1IS覆盖
 */
void vmm_invalidate_itlb(virt_addr_t va) {
  __asm__ volatile("dsb sy\n"
                   "tlbi VAAE1IS, %0\n" // ✅ 包含指令TLB
                   "dsb sy\n"
                   "isb\n" ::"r"(va >> PAGE_SHIFT));
}

/**
 * @brief 使数据TLB失效（通用用法）
 */
void vmm_invalidate_dtlb(virt_addr_t va) {
  __asm__ volatile("dsb sy\n"
                   "tlbi VAAE1IS, %0\n" // ✅ 包含数据TLB
                   "dsb sy\n"
                   "isb\n" ::"r"(va >> PAGE_SHIFT));
}

/*============================================================================
 *                              调试函数
 *============================================================================*/

/**
 * @brief 打印页表信息
 */
void vmm_print_pgt(pgt_t pgt) {
  /* 实际应使用printk遍历打印 */
  (void)pgt;
}

/**
 * @brief 打印VMA信息
 */
void vmm_print_vmas(mm_struct_t *mm) {
  if (!mm) {
    return;
  }

  vma_t *vma = mm->mmap;
  while (vma) {
    /* 实际应使用printk */
    (void)vma;
    vma = vma->vm_next;
  }
}

/**
 * @brief 打印虚拟内存统计信息
 */
void vmm_print_stats(void) { /* 实际应使用printk */ }
