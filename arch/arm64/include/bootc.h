#ifndef BOOTC_H
#define BOOTC_H
/* 1. 核心配置：在这里指定你需要映射的总内存大小 */
#define TOTAL_MEM_SIZE (128 * 1024 * 1024) // 改成 128MB
/* 2. 页表基础常量（保持你的定义） */
#define PHYS_BASE 0x40000000UL
#define KERNEL_VIRT_BASE (0xFFFF800000000000UL)
#define TABLE_SIZE 512                        // 每个表的条目数
#define L3_TABLE_MAP_SIZE (TABLE_SIZE * 4096) // 每个L3表映射 2MB
/* 3. 通用向上取整计算宏（C语言标准写法） */
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

/* 提前计算出各级虚拟地址的索引基址，避免循环内重复计算 */
#define L0_INDEX_BASE (((KERNEL_VIRT_BASE + PHYS_BASE) >> 39) & 0x1FF)
#define L1_INDEX_BASE (((KERNEL_VIRT_BASE + PHYS_BASE) >> 30) & 0x1FF)
#define L2_INDEX_BASE (((KERNEL_VIRT_BASE + PHYS_BASE) >> 21) & 0x1FF)
#define L3_INDEX_BASE (((KERNEL_VIRT_BASE + PHYS_BASE) >> 12) & 0x1FF)

/* 4. 自动计算各级页表数量（核心逻辑） */
// L3: 总内存 / 每个L3表映射的大小 (2MB)
#define L3_TABLES_NEEDED                                                       \
  DIV_ROUND_UP(TOTAL_MEM_SIZE + L3_INDEX_BASE * 4096, L3_TABLE_MAP_SIZE)
// L2: 需要多少个L2表 = 总L3表数 / 512 (向上取整)
#define L2_TABLES_NEEDED                                                       \
  DIV_ROUND_UP(L3_TABLES_NEEDED + L2_INDEX_BASE, TABLE_SIZE)
// L1: 需要多少个L1表 = 总L2表数 / 512 (向上取整)
#define L1_TABLES_NEEDED                                                       \
  DIV_ROUND_UP(L2_TABLES_NEEDED + L1_INDEX_BASE, TABLE_SIZE)
// L0: 内核空间通常固定1个L0页表
#define L0_TABLES_NEEDED 1
/* 5. 保持你的原属性定义 */
#define BOOT_DATA __attribute__((section(".boot.data")))
#define BOOT_CODE __attribute__((section(".boot.text")))
#endif
