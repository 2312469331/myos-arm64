/*
 * 注意：这个文件的所有函数必须在 MMU 开启前执行
 * 所以必须链接到物理地址，使用位置无关代码
 */
#include <bootc.h>
#include <types.h>
/* 6. 自动声明页表数组（完全动态） */
// 注意：这里 ttbr0_l0/ttbr1_l0 是L0表，虽然只有1个，但维度保持 [数量][条目数]
// 一致
uint64_t ttbr0_l0[L0_TABLES_NEEDED][TABLE_SIZE]
    __attribute__((section(".pagetable"), aligned(4096)));
uint64_t ttbr1_l0[L0_TABLES_NEEDED][TABLE_SIZE]
    __attribute__((section(".pagetable"), aligned(4096)));
// L1表：数量 = L1_TABLES_NEEDED
uint64_t l1_table[L1_TABLES_NEEDED][TABLE_SIZE]
    __attribute__((section(".pagetable"), aligned(4096)));
// L2表：数量 = L2_TABLES_NEEDED
uint64_t l2_table[L2_TABLES_NEEDED][TABLE_SIZE]
    __attribute__((section(".pagetable"), aligned(4096)));
// L3表：数量 = L3_TABLES_NEEDED (彻底去掉了 MAX_L3_TABLES=8)
uint64_t l3_tables[L3_TABLES_NEEDED][TABLE_SIZE]
    __attribute__((section(".pagetable"), aligned(4096)));

/* 清空页表（这部分本身就是通用的，无需大改） */
static BOOT_CODE void clear_page_tables(void) {
  for (uint64_t i = 0; i < L0_TABLES_NEEDED; i++)
    for (uint64_t j = 0; j < TABLE_SIZE; j++) {
      ttbr0_l0[i][j] = 0;
      ttbr1_l0[i][j] = 0;
    }

  for (uint64_t i = 0; i < L1_TABLES_NEEDED; i++)
    for (uint64_t j = 0; j < TABLE_SIZE; j++)
      l1_table[i][j] = 0;

  for (uint64_t i = 0; i < L2_TABLES_NEEDED; i++)
    for (uint64_t j = 0; j < TABLE_SIZE; j++)
      l2_table[i][j] = 0;

  for (uint64_t i = 0; i < L3_TABLES_NEEDED; i++)
    for (uint64_t j = 0; j < TABLE_SIZE; j++)
      l3_tables[i][j] = 0;
}

/* L0 表初始化：通用循环，自动跨越 L0 边界 */
static BOOT_CODE void init_l0_tables(void) {
  // 将所有需要的 L1 表挂载到 TTBR1 的 L0 表中
  for (uint64_t i = 0; i < L1_TABLES_NEEDED; i++) {
    // i / 512 : 当前 L1 表落在了第几个物理 L0 表上
    // (L0_INDEX_BASE + i) % 512 : 在该 L0 表内的具体索引
    uint64_t l0_tbl_idx = i / TABLE_SIZE;
    uint64_t l0_entry_idx = (L0_INDEX_BASE + i) % TABLE_SIZE;

    ttbr1_l0[l0_tbl_idx][l0_entry_idx] = (1ULL << 0);  // Valid
    ttbr1_l0[l0_tbl_idx][l0_entry_idx] |= (1ULL << 1); // Table
    ttbr1_l0[l0_tbl_idx][l0_entry_idx] |= (uint64_t)&l1_table[i] & ~0xFFFUL;
  }
  // TTBR0 的 L0 暂不配置，清空防野指针
  ttbr0_l0[0][0] =
      (1ULL << 0) | (1ULL << 1) | ((uint64_t)l1_table[0] & (~0xFFFUL));
}

/* L1 表初始化：通用循环，自动跨越 L1 边界 */
static BOOT_CODE void init_l1_table(void) {
  for (uint64_t i = 0; i < L2_TABLES_NEEDED; i++) {
    uint64_t l1_tbl_idx = i / TABLE_SIZE;
    uint64_t l1_entry_idx = (L1_INDEX_BASE + i) % TABLE_SIZE;

    l1_table[l1_tbl_idx][l1_entry_idx] = (1ULL << 0);  // Valid
    l1_table[l1_tbl_idx][l1_entry_idx] |= (1ULL << 1); // Table
    l1_table[l1_tbl_idx][l1_entry_idx] |= (uint64_t)&l2_table[i] & ~0xFFFUL;
  }
}

/* L2 表初始化：通用循环，自动跨越 L2 边界 */
static BOOT_CODE void init_l2_table(void) {
  for (uint64_t i = 0; i < L3_TABLES_NEEDED; i++) {
    uint64_t l2_tbl_idx = i / TABLE_SIZE;
    uint64_t l2_entry_idx = (L2_INDEX_BASE + i) % TABLE_SIZE;

    l2_table[l2_tbl_idx][l2_entry_idx] = (1ULL << 0);  // Valid
    l2_table[l2_tbl_idx][l2_entry_idx] |= (1ULL << 1); // Table
    l2_table[l2_tbl_idx][l2_entry_idx] |= (uint64_t)&l3_tables[i] & ~0xFFFUL;
  }
}

/* L3 页表初始化：直接通过数学关系计算物理地址，去掉了累加器 */
static BOOT_CODE void init_l3_table(void) {
  // 提取普通内存属性 (Normal Memory)
  uint64_t mem_attr = (1ULL << 0) |  // Valid
                      (1ULL << 1) |  // Page
                      (1ULL << 11) | // nG
                      (1ULL << 10) | // AF
                      (2ULL << 8) |  // SH: Outer Shareable
                      (0ULL << 6) |  // AP: EL1 RW, EL0 No
                      (0ULL << 5) |  // NS: Secure
                      (0ULL << 2);   // AttrIndx: Normal

  // 提取设备内存属性 (Device Memory, 用于UART)
  uint64_t dev_attr = mem_attr | (2ULL << 2); // 仅替换 AttrIndx 为 Device

  uint64_t mapped_bytes = 0;

  for (uint64_t table_idx = 0; table_idx < L3_TABLES_NEEDED; table_idx++) {
    for (uint64_t page_idx = 0; page_idx < TABLE_SIZE; page_idx++) {
      if (mapped_bytes < TOTAL_MEM_SIZE ) {
        // 映射物理内存：基地址 + 当前已映射字节数
        uint64_t pa = PHYS_BASE + mapped_bytes;
        l3_tables[table_idx][page_idx] = mem_attr | (pa & ~0xFFFUL);
        mapped_bytes += 4096;
      } else if (table_idx == L3_TABLES_NEEDED - 1 && page_idx == 511) {
        // 兜底处理：在最后一张L3表的最后一个槽位塞入 UART0
        uint64_t uart_pa = 0x09000000UL;
        l3_tables[table_idx][page_idx] = dev_attr | (uart_pa & ~0xFFFUL);
      }
    }
    //   if (mapped_bytes < TOTAL_MEM_SIZE &&
    //       !(table_idx == L3_TABLES_NEEDED - 1 && page_idx == 511)) {
    //     // 映射物理内存：基地址 + 当前已映射字节数
    //     uint64_t pa = PHYS_BASE + mapped_bytes;
    //     l3_tables[table_idx][page_idx] = mem_attr | (pa & ~0xFFFUL);
    //     mapped_bytes += 4096;
    //   } else if (table_idx == L3_TABLES_NEEDED - 1 && page_idx == 511) {
    //     // 兜底处理：在最后一张L3表的最后一个槽位塞入 UART0
    //     uint64_t uart_pa = 0x09000000UL;
    //     l3_tables[table_idx][page_idx] = dev_attr | (uart_pa & ~0xFFFUL);
    //   }
    // }
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
  tcr |= (2UL << 32); // IPS=0110 → 48位物理地址

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
