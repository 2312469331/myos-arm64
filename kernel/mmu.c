#include <mmu.h>
#include <types.h>
#include <libc.h>
#include <printk.h>

/* 全局变量定义 */
uintptr_t slab_linear_map_base;
phys_addr_t slab_l0_table_pa;

/* 读取 TTBR0_EL1 */
static inline phys_addr_t read_ttbr0(void) {
    uint64_t val;
    __asm__ volatile ("mrs %0, ttbr0_el1" : "=r"(val));
    return (phys_addr_t)val;
}

/* 读取 TTBR1_EL1 */
static inline phys_addr_t read_ttbr1(void) {
    uint64_t val;
    __asm__ volatile ("mrs %0, ttbr1_el1" : "=r"(val));
    return (phys_addr_t)val;
}

/* 判断地址是用户态 (VA[47]=0) 还是内核态 (VA[47]=1) */
static inline int is_user_addr(uintptr_t va) {
    return (va >> 47) == 0;
}

/* 页表操作函数 */
int arm64_map_one_page(uintptr_t va, phys_addr_t pa, uint64_t prot) {
    pte_t *l0, *l1, *l2, *l3;
    phys_addr_t l0_pa, l1_pa, l2_pa, l3_pa;
    unsigned long idx0, idx1, idx2, idx3;

    /* 根据地址选择 L0 表：用户态走 TTBR0，内核态走 TTBR1 */
    if (is_user_addr(va)) {
        l0_pa = read_ttbr0();
    } else {
        l0_pa = read_ttbr1();
    }

    if (!l0_pa)
        return -1;

    l0 = (pte_t *)phys_to_virt(l0_pa);

    idx0 = L0_INDEX(va);
    idx1 = L1_INDEX(va);
    idx2 = L2_INDEX(va);
    idx3 = L3_INDEX(va);

    /* 1. 检查/创建 L1 表 */
    if (!pte_present(l0[idx0])) {
        l1_pa = alloc_phys_pages(0, GFP_KERNEL);
        if (!l1_pa)
            return -1;
        memset(phys_to_virt(l1_pa), 0, PAGE_SIZE);
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
        memset(phys_to_virt(l2_pa), 0, PAGE_SIZE);
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
        memset(phys_to_virt(l3_pa), 0, PAGE_SIZE);
        l2[idx2] = ARM64_TABLE_PROT | l3_pa;
    } else {
        l3_pa = pte_to_phys(l2[idx2]);
    }
    l3 = (pte_t *)phys_to_virt(l3_pa);

    /* 4. 映射页面 - prot 直接作为 PTE 标志 */
    l3[idx3] = prot | (pa & ARM64_PTE_ADDR_MASK);

    return 0;
}

void arm64_unmap_one_page(uintptr_t va) {
  pte_t *l0, *l1, *l2, *l3;
  phys_addr_t l0_pa, l1_pa, l2_pa, l3_pa;
  unsigned long idx0, idx1, idx2, idx3;

  /* 根据地址选择 L0 表 */
  if (is_user_addr(va)) {
    l0_pa = read_ttbr0();
  } else {
    l0_pa = read_ttbr1();
  }

  if (!l0_pa)
    return;

  l0 = (pte_t *)phys_to_virt(l0_pa);

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

/* 读取 TTBR0_EL1（供 Rust FFI 调用） */
phys_addr_t read_ttbr0_el1(void) {
    uint64_t val;
    __asm__ volatile ("mrs %0, ttbr0_el1" : "=r"(val));
    return (phys_addr_t)val;
}

/* 读取 TTBR1_EL1（供 Rust FFI 调用） */
phys_addr_t read_ttbr1_el1(void) {
    uint64_t val;
    __asm__ volatile ("mrs %0, ttbr1_el1" : "=r"(val));
    return (phys_addr_t)val;
}

/* 刷新 TLB */
void flush_tlb(void) {
  __asm__ volatile ("tlbi vmalle1" : : : "memory");
  __asm__ volatile ("dsb sy" : : : "memory");
  __asm__ volatile ("isb" : : : "memory");
}

/**
 * copy_user_page_table - 复制用户态页表树
 * @src_pgd_pa: 源页表 L0 物理地址
 * @dst_pgd_pa: 目标页表 L0 物理地址
 *
 * 遍历源页表的用户态条目 (VA[47]=0)，在目标页表建立相同映射。
 * 物理页被共享（两个页表指向同一物理页），后续 COW 时再分离。
 * 不复制内核态条目（由 TTBR1 共享）。
 */
void copy_user_page_table(phys_addr_t src_pgd_pa, phys_addr_t dst_pgd_pa) {
    pte_t *src_l0 = (pte_t *)phys_to_virt(src_pgd_pa);
    pte_t *dst_l0 = (pte_t *)phys_to_virt(dst_pgd_pa);

    /* 只遍历用户态条目：L0 index 0..511 (VA[47]=0) */
    for (unsigned long idx0 = 0; idx0 < PTRS_PER_PTE; idx0++) {
        if (!pte_present(src_l0[idx0]))
            continue;

        pte_t *src_l1 = (pte_t *)phys_to_virt(pte_to_phys(src_l0[idx0]));

        /* 目标 L1 表不存在则分配并清零 */
        if (!pte_present(dst_l0[idx0])) {
            phys_addr_t dst_l1_pa = alloc_phys_pages(0, GFP_KERNEL);
            if (!dst_l1_pa) continue;
            memset(phys_to_virt(dst_l1_pa), 0, PAGE_SIZE);
            dst_l0[idx0] = ARM64_TABLE_PROT | dst_l1_pa;
        }
        pte_t *dst_l1 = (pte_t *)phys_to_virt(pte_to_phys(dst_l0[idx0]));

        for (unsigned long idx1 = 0; idx1 < PTRS_PER_PTE; idx1++) {
            if (!pte_present(src_l1[idx1]))
                continue;

            pte_t *src_l2 = (pte_t *)phys_to_virt(pte_to_phys(src_l1[idx1]));

            if (!pte_present(dst_l1[idx1])) {
                phys_addr_t dst_l2_pa = alloc_phys_pages(0, GFP_KERNEL);
                if (!dst_l2_pa) continue;
                memset(phys_to_virt(dst_l2_pa), 0, PAGE_SIZE);
                dst_l1[idx1] = ARM64_TABLE_PROT | dst_l2_pa;
            }
            pte_t *dst_l2 = (pte_t *)phys_to_virt(pte_to_phys(dst_l1[idx1]));

            for (unsigned long idx2 = 0; idx2 < PTRS_PER_PTE; idx2++) {
                if (!pte_present(src_l2[idx2]))
                    continue;

                pte_t *src_l3 = (pte_t *)phys_to_virt(pte_to_phys(src_l2[idx2]));

                if (!pte_present(dst_l2[idx2])) {
                    phys_addr_t dst_l3_pa = alloc_phys_pages(0, GFP_KERNEL);
                    if (!dst_l3_pa) continue;
                    memset(phys_to_virt(dst_l3_pa), 0, PAGE_SIZE);
                    dst_l2[idx2] = ARM64_TABLE_PROT | dst_l3_pa;
                }
                pte_t *dst_l3 = (pte_t *)phys_to_virt(pte_to_phys(dst_l2[idx2]));

                /* 复制 L3 页表项：共享物理页，子进程可写页标 COW 只读 */
                for (unsigned long idx3 = 0; idx3 < PTRS_PER_PTE; idx3++) {
                    if (!pte_present(src_l3[idx3]))
                        continue;

                    pte_t pte = src_l3[idx3];

                    /* 子进程：可写页标只读，触发写时复制 */
                    if ((pte & (1UL << 6)) && !(pte & (1UL << 7))) {
                        pte |= (1UL << 7);
                    }

                    dst_l3[idx3] = pte;

                    /* 增加共享数据页的引用计数 */
                    get_page(pte_to_phys(src_l3[idx3]));
                }
            }
        }
    }
}

/**
 * free_page_table_tree - 递归释放整个页表树
 * @pgd_pa: L0 页表物理地址
 *
 * 释放 L0→L1→L2→L3 所有页表页，以及 L3 中映射的数据页（通过 put_page 递减引用计数）。
 */
void free_page_table_tree(phys_addr_t pgd_pa) {
    pte_t *l0 = (pte_t *)phys_to_virt(pgd_pa);

    for (unsigned long idx0 = 0; idx0 < PTRS_PER_PTE; idx0++) {
        if (!pte_present(l0[idx0]))
            continue;

        pte_t *l1 = (pte_t *)phys_to_virt(pte_to_phys(l0[idx0]));

        for (unsigned long idx1 = 0; idx1 < PTRS_PER_PTE; idx1++) {
            if (!pte_present(l1[idx1]))
                continue;

            pte_t *l2 = (pte_t *)phys_to_virt(pte_to_phys(l1[idx1]));

            for (unsigned long idx2 = 0; idx2 < PTRS_PER_PTE; idx2++) {
                if (!pte_present(l2[idx2]))
                    continue;

                pte_t *l3 = (pte_t *)phys_to_virt(pte_to_phys(l2[idx2]));

                /* 释放 L3 中映射的数据页 */
                for (unsigned long idx3 = 0; idx3 < PTRS_PER_PTE; idx3++) {
                    if (pte_present(l3[idx3])) {
                        put_page(pte_to_phys(l3[idx3]));
                    }
                }

                /* 释放 L3 页表页 */
                free_phys_pages(pte_to_phys(l2[idx2]), 0);
            }

            /* 释放 L2 页表页 */
            free_phys_pages(pte_to_phys(l1[idx1]), 0);
        }

        /* 释放 L1 页表页 */
        free_phys_pages(pte_to_phys(l0[idx0]), 0);
    }

    /* 释放 L0 页表页 */
    free_phys_pages(pgd_pa, 0);
}

/* 从页表中读取虚拟地址对应的物理地址，未映射返回 0 */
phys_addr_t arm64_get_phys_from_va(uintptr_t va) {
  pte_t *l0, *l1, *l2, *l3;
  phys_addr_t l0_pa;

  if (is_user_addr(va)) {
    l0_pa = read_ttbr0();
  } else {
    l0_pa = read_ttbr1();
  }

  if (!l0_pa) return 0;

  l0 = (pte_t *)phys_to_virt(l0_pa);
  if (!pte_present(l0[L0_INDEX(va)])) return 0;

  l1 = (pte_t *)phys_to_virt(pte_to_phys(l0[L0_INDEX(va)]));
  if (!pte_present(l1[L1_INDEX(va)])) return 0;

  l2 = (pte_t *)phys_to_virt(pte_to_phys(l1[L1_INDEX(va)]));
  if (!pte_present(l2[L2_INDEX(va)])) return 0;

  l3 = (pte_t *)phys_to_virt(pte_to_phys(l2[L2_INDEX(va)]));
  if (!pte_present(l3[L3_INDEX(va)])) return 0;

  return pte_to_phys(l3[L3_INDEX(va)]);
}

/// 调试：以树状格式打印页表结构
/// @param pgd_pa  L0 页表物理地址
void dump_page_table(phys_addr_t pgd_pa) {
    pte_t *l0 = (pte_t *)phys_to_virt(pgd_pa);

    printk("[PT] pgd=0x%lx\n", (unsigned long)pgd_pa);

    for (unsigned long idx0 = 0; idx0 < PTRS_PER_PTE; idx0++) {
        if (!pte_present(l0[idx0]))
            continue;

        unsigned long l0_pa = (unsigned long)pte_to_phys(l0[idx0]);
        printk("  L0[%3lu] -> 0x%lx\n", idx0, l0_pa);

        pte_t *l1 = (pte_t *)phys_to_virt(pte_to_phys(l0[idx0]));
        for (unsigned long idx1 = 0; idx1 < PTRS_PER_PTE; idx1++) {
            if (!pte_present(l1[idx1]))
                continue;

            unsigned long l1_pa = (unsigned long)pte_to_phys(l1[idx1]);
            printk("    L1[%3lu] -> 0x%lx\n", idx1, l1_pa);

            pte_t *l2 = (pte_t *)phys_to_virt(pte_to_phys(l1[idx1]));
            for (unsigned long idx2 = 0; idx2 < PTRS_PER_PTE; idx2++) {
                if (!pte_present(l2[idx2]))
                    continue;

                unsigned long l2_pa = (unsigned long)pte_to_phys(l2[idx2]);

                if (l2[idx2] & ARM64_PTE_TYPE_TABLE) {
                    printk("      L2[%3lu] -> 0x%lx  [table]\n", idx2, l2_pa);

                    pte_t *l3 = (pte_t *)phys_to_virt(pte_to_phys(l2[idx2]));
                    for (unsigned long idx3 = 0; idx3 < PTRS_PER_PTE; idx3++) {
                        if (!pte_present(l3[idx3]))
                            continue;

                        unsigned long va = (idx0 << 39) | (idx1 << 30) |
                                          (idx2 << 21) | (idx3 << 12);
                        unsigned long pa = (unsigned long)pte_to_phys(l3[idx3]);
                        unsigned long prot = l3[idx3] & ~ARM64_PTE_ADDR_MASK;
                        printk("        L3[%3lu] va=%#018lx -> pa=0x%lx  prot=%#010lx\n",
                            idx3, va, pa, prot);
                    }
                } else {
                    unsigned long va = (idx0 << 39) | (idx1 << 30) | (idx2 << 21);
                    unsigned long prot = l2[idx2] & ~ARM64_PTE_ADDR_MASK;
                    printk("      L2[%3lu] va=%#018lx -> pa=0x%lx  [block] prot=%#010lx\n",
                        idx2, va, l2_pa, prot);
                }
            }
        }
    }
}