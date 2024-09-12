/*
 * 注意：这个文件的所有函数必须在 MMU 开启前执行
 * 所以必须链接到物理地址，使用位置无关代码
 */

#include <stdint.h>

/* 强制放到 .boot.data 段 */
#define BOOT_DATA __attribute__((section(".boot.data")))
#define BOOT_CODE __attribute__((section(".boot.text")))
/* 页表项位定义 */
#define PTE_VALID (1UL << 0)       /* 条目有效位 Valid=1 */
#define PTE_TYPE_TABLE (0UL << 1)  /* 类型=0：指向下一级页表 */
#define PTE_TYPE_PAGE (1UL << 1)   /* 类型=1：指向4KB物理页 */
#define PTE_ATTR_NORMAL (0UL << 2) /* AttrIndx=0：Normal Memory (WB RAWA) */
#define PTE_ATTR_DEVICE (2UL << 2) /* AttrIndx=2：Device-nGnRnE (MMIO) */
#define PTE_AP_FULL_RW (11UL << 7) /* AP=11：EL0/EL1均可读写 */
#define PTE_AP_EL1_RW (01UL << 7)  /* AP=01：EL1读写，EL0不可访问 */
#define PTE_SHARE_OUTER (2UL << 8) /* Shareable=10：Outer Shareable */
#define PTE_AF (1UL << 10)         /* Access Flag=1：已访问 */
#define PTE_NS (1UL << 6)          /* NS=1：Non-secure */

/* 地址常量 */
#define PHYS_BASE 0x40000000UL
#define USER_VIRT_BASE 0x40000000UL
#define KERNEL_VIRT_BASE 0xFFFF000000000000UL

/* 页表外部声明（链接脚本.pagetable段） */
uint64_t ttbr0_l0[512] __attribute__((section(".pagetable"), aligned(4096)));
uint64_t ttbr1_l0[512] __attribute__((section(".pagetable"), aligned(4096)));
uint64_t l1_table[512] __attribute__((section(".pagetable"), aligned(4096)));
uint64_t l2_table[512] __attribute__((section(".pagetable"), aligned(4096)));
uint64_t l3_table[512] __attribute__((section(".pagetable"), aligned(4096)));

/* 清空所有页表条目 */
static BOOT_CODE void clear_page_tables(void) {
  for (uint64_t i = 0; i < 512; i++) {
    ttbr0_l0[i] = 0;
    ttbr1_l0[i] = 0;
    l1_table[i] = 0;
    l2_table[i] = 0;
    l3_table[i] = 0;
  }
}

/* 初始化L0页表（TTBR0/TTBR1） */
static BOOT_CODE void init_l0_tables(void) {
  /* TTBR0：0x40000000的L0索引=0（bit47-39=0） */
  ttbr0_l0[0] = PTE_VALID | PTE_TYPE_TABLE | PTE_AP_FULL_RW | PTE_SHARE_OUTER |
                PTE_AF | PTE_NS;
  ttbr0_l0[0] |= (uint64_t)l1_table & ~0xFFF; /* 指向L1页表 */

  /* TTBR1：0xFFFF000000000000的L0索引=511（bit47-39=0x1FF） */
  ttbr1_l0[511] = PTE_VALID | PTE_TYPE_TABLE | PTE_AP_EL1_RW | PTE_SHARE_OUTER |
                  PTE_AF | PTE_NS;
  ttbr1_l0[511] |= (uint64_t)l1_table & ~0xFFF; /* 共享L1页表 */
}

/* 初始化L1页表 */
static BOOT_CODE void init_l1_table(void) {
  /* 0x40000000的L1索引=2（bit38-30=2） */
  l1_table[2] = PTE_VALID | PTE_TYPE_TABLE | PTE_AP_FULL_RW | PTE_SHARE_OUTER |
                PTE_AF | PTE_NS;
  l1_table[2] |= (uint64_t)l2_table & ~0xFFF; /* 指向L2页表 */
}

/* 初始化L2页表 */
static BOOT_CODE void init_l2_table(void) {
  /* 0x40000000的L2索引=0（bit29-21=0） */
  l2_table[0] = PTE_VALID | PTE_TYPE_TABLE | PTE_AP_FULL_RW | PTE_SHARE_OUTER |
                PTE_AF | PTE_NS;
  l2_table[0] |= (uint64_t)l3_table & ~0xFFF; /* 指向L3页表 */
}

/* 初始化L3页表（映射4KB物理页） */
static BOOT_CODE void init_l3_table(void) {
  /* 映射0x40000000~0x401FFFFF（2MB，512个4KB页） */
  for (uint64_t i = 0; i < 512; i++) {
    uint64_t pa = PHYS_BASE + (i << 12); /* 物理地址=基地址+页号*4KB */
    l3_table[i] = PTE_VALID | PTE_TYPE_PAGE | PTE_ATTR_NORMAL | PTE_AP_FULL_RW |
                  PTE_SHARE_OUTER | PTE_AF | PTE_NS;
    l3_table[i] |= pa & ~0xFFF; /* 物理地址（4KB对齐） */
  }
}

/* 完整页表初始化入口 */
BOOT_CODE
void init_page_tables(void) {
  clear_page_tables();
  init_l0_tables();
  init_l1_table();
  init_l2_table();
  init_l3_table();
}

/* 获取 TTBR0 物理地址 */
BOOT_CODE
uint64_t get_ttbr0_el1(void) { return (uint64_t)ttbr0_l0; }

/* 获取 TTBR1 物理地址 */
BOOT_CODE
uint64_t get_ttbr1_el1(void) { return (uint64_t)ttbr1_l0; }

/* 获取 TCR_EL1 配置 */
/* 获取 TCR_EL1 完整配置 (48位VA + 4KB页 + 全特性兼容) */
BOOT_CODE
uint64_t get_tcr_el1(void) {
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
  tcr |= (6UL << 32); // IPS=0110 → 48位物理地址

  // --- 高级特性位（bit 39~63）：全部默认0，符合ARM规范 ---
  // 无需额外代码，tcr初始化为0已满足要求

  return tcr;
}

/* 获取 MAIR_EL1 配置 */
BOOT_CODE
uint64_t get_mair_el1(void) {
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
  return mair;
}
/* 获取 SCTLR_EL1 配置 */
/* 获取 SCTLR_EL1 完整配置 (安全+性能+兼容) */
BOOT_CODE
uint64_t get_sctlr_el1(void) {
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

  // --- bit 33~63：全部RES0，符合ARM规范 ---
  return sctlr;
}
