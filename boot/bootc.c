/*
 * 注意：这个文件的所有函数必须在 MMU 开启前执行
 * 所以必须链接到物理地址，使用位置无关代码
 */

#include <stdint.h>

/* 强制放到 .boot.data 段 */
#define BOOT_DATA __attribute__((section(".boot.data")))
#define BOOT_CODE __attribute__((section(".boot.text")))

/* 页表存储区 - 放在 .pagetable 段，4K 对齐 */
BOOT_DATA __attribute__((
    aligned(4096))) static uint64_t ttbr0_pgd[512]; /* 512 个 PGD 条目 */

BOOT_DATA __attribute__((
    aligned(4096))) static uint64_t ttbr1_pgd[512]; /* 512 个 PGD 条目 */

BOOT_DATA __attribute__((
    aligned(4096))) static uint64_t ttbr1_pud[512]; /* 512 个 PUD 条目 */

BOOT_DATA __attribute__((
    aligned(4096))) static uint64_t ttbr1_pmd[512]; /* 512 个 PMD 条目 */

/* 页表项标志位 */
#define PTE_VALID (1UL << 0)
#define PTE_BLOCK (0UL << 1)
#define PTE_TABLE (1UL << 1)
#define PTE_AF (1UL << 10)
#define PTE_SH_INNER (3UL << 8)
#define PTE_AP_RW (0UL << 6)
#define PTE_XN (1UL << 54)
#define PTE_ATTR_IDX(n) ((n) << 2)

#define PAGE_2M (2UL * 1024 * 1024)
#define PAGE_4K (4UL * 1024)

#define PHYS_BASE 0x40000000UL

/* 内存属性：索引 0 = Normal, 索引 1 = Device */
#define MT_NORMAL (PTE_ATTR_IDX(0) | PTE_AF | PTE_SH_INNER | PTE_AP_RW)
#define MT_DEVICE (PTE_ATTR_IDX(1) | PTE_AF | PTE_SH_INNER | PTE_AP_RW | PTE_XN)

/* 创建块描述符 (2M) */
static inline uint64_t create_block(uint64_t pa, uint64_t attr) {
  return (pa & ~(PAGE_2M - 1)) | PTE_VALID | PTE_BLOCK | attr;
}

/* 创建表描述符 */
static inline uint64_t create_table(uint64_t pa) {
  return (pa & ~(PAGE_4K - 1)) | PTE_VALID | PTE_TABLE;
}

/* 清空页表 */
BOOT_CODE
static void clear_pagetable(uint64_t *table, int count) {
  for (int i = 0; i < count; i++) {
    table[i] = 0;
  }
}

/* 初始化 TTBR0 - 1:1 映射 (低地址) */
BOOT_CODE
void init_ttbr0_pagetable(void) {
  uint64_t pa;
  uint64_t va;
  int idx;

  clear_pagetable(ttbr0_pgd, 512);

  /* 映射 0x00000000 - 0x40000000 (设备内存) */
  pa = 0;
  va = 0;
  while (va < 0x40000000) {
    idx = (va >> 30) & 0x1FF; /* VA[38:30] */
    ttbr0_pgd[idx] = create_block(pa, MT_DEVICE);
    va += PAGE_2M;
    pa += PAGE_2M;
  }

  /* 映射 0x40000000 - 0x80000000 (正常内存，1:1) */
  pa = PHYS_BASE;
  va = PHYS_BASE;
  while (va < 0x80000000) {
    idx = (va >> 30) & 0x1FF;
    ttbr0_pgd[idx] = create_block(pa, MT_NORMAL);
    va += PAGE_2M;
    pa += PAGE_2M;
  }
}

/* 初始化 TTBR1 - 虚拟地址映射 (高地址) */
BOOT_CODE
void init_ttbr1_pagetable(void) {
  uint64_t pa;
  uint64_t va;
  int idx;

  clear_pagetable(ttbr1_pgd, 512);
  clear_pagetable(ttbr1_pud, 512);
  clear_pagetable(ttbr1_pmd, 512);

  /* PGD -> PUD (TTBR1 基址) */
  /* VIRT_BASE = 0xffff000000000000, VA[47:39] = 0x1FF */
  idx = 0x1FF;
  ttbr1_pgd[idx] = create_table((uint64_t)ttbr1_pud);

  /* PUD -> PMD, VA[38:30] = 0x1FF */
  idx = 0x1FF;
  ttbr1_pud[idx] = create_table((uint64_t)ttbr1_pmd);

  /* PMD -> 物理地址 (2M 块) */
  pa = PHYS_BASE;
  va = 0xffff000000000000UL;
  while (pa < 0x80000000) {
    /* VA[29:21] 索引 */
    idx = (va >> 21) & 0x1FF;
    ttbr1_pmd[idx] = create_block(pa, MT_NORMAL);
    va += PAGE_2M;
    pa += PAGE_2M;
  }
}

/* 获取 TTBR0 物理地址 */
BOOT_CODE
uint64_t get_ttbr0_el1(void) { return (uint64_t)ttbr0_pgd; }

/* 获取 TTBR1 物理地址 */
BOOT_CODE
uint64_t get_ttbr1_el1(void) { return (uint64_t)ttbr1_pgd; }

/* 获取 TCR_EL1 配置 */
BOOT_CODE
uint64_t get_tcr_el1(void) {
  uint64_t tcr = 0;

  /* T0SZ = 25 (39-bit VA for TTBR0) */
  tcr |= (25UL << 0);
  /* T1SZ = 16 (48-bit VA for TTBR1) */
  tcr |= (16UL << 16);
  /* IPS = 2 (48-bit PA) */
  tcr |= (2UL << 32);
  /* TG0 = 0 (4KB), TG1 = 2 (4KB) */
  tcr |= (0UL << 14);
  tcr |= (2UL << 30);
  /* SH0 = 2, SH1 = 2 (Outer Shareable) */
  tcr |= (2UL << 6);
  tcr |= (2UL << 8);
  /* ORGN0 = 1, ORGN1 = 1 (Write-Back) */
  tcr |= (1UL << 4);
  tcr |= (1UL << 2);
  /* IRGN0 = 1, IRGN1 = 1 (Write-Back) */
  tcr |= (1UL << 10);
  tcr |= (1UL << 12);
  /* A1 = 0, EPD1 = 0, EPD0 = 0 */

  return tcr;
}

/* 获取 MAIR_EL1 配置 */
BOOT_CODE
uint64_t get_mair_el1(void) {
  uint64_t mair = 0;

  /* ATTR0: Normal Memory (WBWA) */
  mair |= (0xFFUL << 0);
  /* ATTR1: Device Memory (nGnRnE) */
  mair |= (0x44UL << 8);

  return mair;
}
