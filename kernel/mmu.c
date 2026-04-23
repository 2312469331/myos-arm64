#
#include "mmu.h"
#include "types.h"

/* 全局变量定义 */
uintptr_t slab_linear_map_base;
phys_addr_t slab_l0_table_pa;

/* 页表操作函数 */
int arm64_map_one_page(uintptr_t va, phys_addr_t pa, uint64_t prot) {

    
    pte_t *l0, *l1, *l2, *l3;
    phys_addr_t l1_pa, l2_pa, l3_pa;
    unsigned long idx0, idx1, idx2, idx3;

    if (!l0_table_pa)
        return -1;

    l0 = (pte_t *)phys_to_virt(l0_table_pa);

    idx0 = L0_INDEX(va);
    idx1 = L1_INDEX(va);
    idx2 = L2_INDEX(va);
    idx3 = L3_INDEX(va);

    /* 1. 检查/创建 L1 表 */
    if (!pte_present(l0[idx0])) {
        l1_pa = alloc_phys_pages(0, GFP_KERNEL);
        if (!l1_pa)
            return -1;
        l0[idx0] = ARM64_TABLE_PROT | l1_pa;
    } else {
        l1_pa = pte_to_phys(l0[idx0]);
    }
    l1 = (pte_t *)phys_to_virt(l1_pa);

    /* 2. 检查/创建 L2 表 */
    if (!pte_present(l1[idx1])) {
        l2_pa = alloc_phys_pages(0, GFP_KERNEL);
        if (!l2_pa)
            return -1;
        l1[idx1] = ARM64_TABLE_PROT | l2_pa;
    } else {
        l2_pa = pte_to_phys(l1[idx1]);
    }
    l2 = (pte_t *)phys_to_virt(l2_pa);

    /* 3. 检查/创建 L3 表 */
    if (!pte_present(l2[idx2])) {
        l3_pa = alloc_phys_pages(0, GFP_KERNEL);
        if (!l3_pa)
            return -1;
        l2[idx2] = ARM64_TABLE_PROT | l3_pa;
    } else {
        l3_pa = pte_to_phys(l2[idx2]);
    }
    l3 = (pte_t *)phys_to_virt(l3_pa);

    /* 4. 映射页面 - 根据 prot 参数选择合适的页表属性 */
    uint64_t attr_bits;
    
    // 检查 prot 参数是否为 GFP_* 标志
    if (prot == GFP_KERNEL) { // GFP_KERNEL
        // 常规内核分配 - 使用内核可读写属性
        attr_bits = PAGE_KERNEL_RW;
    } else if (prot == GFP_ATOMIC) { // GFP_ATOMIC
        // 原子分配 - 使用内核可读写属性
        attr_bits = PAGE_KERNEL_RW;
    } else if (prot == GFP_DMA) { // GFP_DMA
        // DMA 分配 - 使用非共享属性
        attr_bits = PAGE_DMA;
    } else if (prot == GFP_HIGHMEM) { // GFP_HIGHMEM
        // 高端内存 - 使用内核可读写属性
        attr_bits = PAGE_KERNEL_RW;
    } else {
      return -1;
    }
    
    // 组合属性位和物理地址
    l3[idx3] = attr_bits | (pa & ARM64_PTE_ADDR_MASK);

    return 0;
}

void arm64_unmap_one_page(uintptr_t va) {
  
  pte_t *l0, *l1, *l2, *l3;
  phys_addr_t l1_pa, l2_pa, l3_pa;
  unsigned long idx0, idx1, idx2, idx3;

  if (!l0_table_pa)
    return;

  l0 = (pte_t *)phys_to_virt(l0_table_pa);

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

/* 设置 MAIR_EL1 寄存器 */
void set_mair_el1(void) {
  uint64_t mair = 0;
  /* Attr0 = 0xFF: Normal Memory, Outer/Inner Write-Back RAWA (可缓存RAM), */
  mair |= (0xFFUL << 0);
  /* Attr1 = 0x44: Normal Memory, Outer/Inner Non-cacheable (不可缓存RAM/ DMA),
   */
  mair |= (0x44UL << 8);
  /* Attr2 = 0x00: Device Memory, Device-nGnRnE (用于MMIO外设), */
  mair |= (0x00UL << 16);
  /* Attr3~Attr7 = 0x00: 同上, 设备内存, 保留位为0 */
  mair |= (0x00UL << 24);
  mair |= (0x00UL << 32);
  mair |= (0x00UL << 40);
  mair |= (0x00UL << 48);
  mair |= (0x00UL << 56);
  
  __asm__ volatile ("msr mair_el1, %0" : : "r"(mair));
}

/* 设置 TCR_EL1 寄存器 */
void set_tcr_el1(void) {
  uint64_t tcr = 0;

  // --- 用户态 (TTBR0) 核心配置 ---
  tcr |= (16UL << 0); // T0SZ=16 → 48位虚拟地址
  tcr |= (0UL << 14); // TG0=00 → 4KB页
  tcr |= (2UL << 12); // SH0=10 → Outer Shareable（多核缓存一致性）
  tcr |= (1UL << 10); // ORGN0=01 → Write-Back缓存（性能最优）
  tcr |= (1UL << 8);  // IRGN0=01 → Write-Back缓存（性能最优）

  // --- 内核态 (TTBR1) 核心配置 ---
  tcr |= (16UL << 16); // T1SZ=16 → 48位虚拟地址
  tcr |= (2UL << 30);  // TG1=10 → 4KB页
  tcr |= (2UL << 28);  // SH1=10 → Outer Shareable（多核缓存一致性）
  tcr |= (1UL << 26);  // ORGN1=01 → Write-Back缓存（性能最优）
  tcr |= (1UL << 24);  // IRGN1=01 → Write-Back缓存（性能最优）

  // --- 物理地址与系统配置 ---
  tcr |= (2UL << 32); // IPS=0110 → 48位物理地址

  __asm__ volatile ("msr tcr_el1, %0" : : "r"(tcr));
}

/* 设置 SCTLR_EL1 寄存器 */
void set_sctlr_el1(void) {
  uint64_t sctlr = 0;

  // --- bit 0~14：核心 MMU/缓存/对齐/安全 ---
  sctlr |= (1UL << 0);  // M=1: 启用MMU
  sctlr |= (1UL << 1);  // A=1: 启用对齐检查
  sctlr |= (1UL << 2);  // C=1: 启用数据缓存
  sctlr |= (1UL << 3);  // SA=1: EL1 SP对齐检查
  sctlr |= (1UL << 4);  // SA0=1: EL0 SP对齐检查
  sctlr |= (0UL << 5);  // CP15BEN=0: 禁用AArch32 CP15指令
  sctlr |= (0UL << 6);  // nAA=0: 严格对齐
  sctlr |= (1UL << 7);  // ITD=1: 禁用AArch32 IT指令
  sctlr |= (1UL << 8);  // SED=1: 禁用AArch32 SETEND指令
  sctlr |= (0UL << 9);  // UMA=0: 禁止EL0访问DAIF
  sctlr |= (0UL << 10); // EnRCTX=0: 禁用EL0访问SPECRES指令
  sctlr |= (1UL << 11); // EOS=1: 异常返回上下文同步
  sctlr |= (1UL << 12); // I=1: 启用指令缓存
  sctlr |= (0UL << 13); // EnDB=0: 禁用PAuth数据地址认证
  sctlr |= (0UL << 14); // DZE=0: 禁止EL0执行DC ZVA

  // --- bit 15~20：EL0陷阱与安全 ---
  sctlr |= (0UL << 15); // UCT=0: 陷阱EL0访问CTR_EL0
  sctlr |= (0UL << 16); // nTWI=0: 陷阱EL0执行WFI
  sctlr |= (0UL << 17); // RES0
  sctlr |= (0UL << 18); // nTWE=0: 陷阱EL0执行WFE
  sctlr |= (1UL << 19); // WXN=1: 写权限=不可执行
  sctlr |= (0UL << 20); // TSCXT=0: 陷阱EL0访问SCXTNUM_EL0

  // --- bit 21~24：异常同步与端序 ---
  sctlr |= (1UL << 21); // IESB=1: 隐式错误同步
  sctlr |= (1UL << 22); // EIS=1: 异常入口同步
  sctlr |= (1UL << 23); // SPAN=1: 特权访问永不
  sctlr |= (0UL << 24); // E0E=0: EL0小端

  // --- bit 25~28：端序、缓存陷阱与指针认证 ---
  sctlr |= (0UL << 25); // EE=0: EL1小端
  sctlr |= (0UL << 26); // UCI=0: 陷阱EL0缓存指令
  sctlr |= (0UL << 27); // EnDA=0: 禁用PAuth数据地址认证
  sctlr |= (0UL << 28); // nTLSMD=0: 陷阱A32/T32批量设备访问

  // --- bit 29~32：原子性、指针认证与缓存权限 ---
  sctlr |= (1UL << 29); // LSMAOE=1: 保证批量访问原子性
  sctlr |= (0UL << 30); // EnIB=0: 禁用PAuth指令地址认证
  sctlr |= (0UL << 31); // EnIA=0: 禁用PAuth指令地址认证
  sctlr |= (0UL << 32); // CMOW=0: 缓存权限安全配置

  __asm__ volatile ("msr sctlr_el1, %0" : : "r"(sctlr));
}

/* 切换 TTBR0_EL1 寄存器（用户页表） */
void switch_ttbr0(phys_addr_t ttbr0_pa) {
  __asm__ volatile ("msr ttbr0_el1, %0" : : "r"(ttbr0_pa));
  flush_tlb();
}

/* 刷新 TLB */
void flush_tlb(void) {
  __asm__ volatile ("tlbi vmalle1" : : : "memory");
  __asm__ volatile ("dsb sy" : : : "memory");
  __asm__ volatile ("isb" : : : "memory");
}