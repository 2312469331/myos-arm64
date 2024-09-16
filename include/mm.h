/**
 * @file mm.h
 * @brief ARM64 内存管理总头文件
 * 
 * 提供内存管理的公共定义、类型声明和宏定义。
 * 包含物理地址、虚拟地址转换，页表属性定义等。
 */

#ifndef _MM_H
#define _MM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*============================================================================
 *                              地址常量定义
 *============================================================================*/

/* 物理内存基地址 - 内核代码加载位置 */
#define PHYS_BASE           0x40000000UL

/* 内核虚拟地址基地址 - 高位内核空间 */
#define KERNEL_VIRT_BASE    0xFFFF000000000000UL

/* 物理内存与虚拟内存转换宏 */
#define PHYS_TO_VIRT(pa)    ((void*)((uint64_t)(pa) + KERNEL_VIRT_BASE - PHYS_BASE))
#define VIRT_TO_PHYS(va)    ((uint64_t)(va) - KERNEL_VIRT_BASE + PHYS_BASE)

/* 内核镜像大小 (2MB) */
#define KERNEL_IMAGE_SIZE   (2UL * 1024 * 1024)

/* 内核空间结束地址 */
#define KERNEL_VIRT_END     (KERNEL_VIRT_BASE + KERNEL_IMAGE_SIZE)

/*============================================================================
 *                              页面大小定义
 *============================================================================*/

/* 页面大小 */
#define PAGE_SIZE           4096UL              /* 4KB */
#define PAGE_SHIFT          12                  /* log2(PAGE_SIZE) */
#define PAGE_MASK           (~(PAGE_SIZE - 1))  /* 页对齐掩码 */

/* 大页大小 */
#define PAGE_SIZE_2M        (2UL * 1024 * 1024) /* 2MB 大页 */
#define PAGE_SHIFT_2M       21                  /* log2(2MB) */

#define PAGE_SIZE_1G        (1024UL * 1024 * 1024) /* 1GB 大页 */
#define PAGE_SHIFT_1G       30                  /* log2(1GB) */

/* 页对齐宏 */
#define PAGE_ALIGN(addr)    (((addr) + PAGE_SIZE - 1) & PAGE_MASK)
#define IS_PAGE_ALIGNED(addr) (((uint64_t)(addr) & (PAGE_SIZE - 1)) == 0)

/*============================================================================
 *                              ARM64 页表属性
 *============================================================================*/

/**
 * @brief 页表项属性位定义 (ARMv8-A)
 * 
 * Lower attributes (bits [11:0]):
 * - Bit 0: Valid (V)
 * - Bit 1: Block/Table
 * - Bit 2: Memory Attribute Index (MAIndex) [0]
 * - Bit 3: Memory Attribute Index (MAIndex) [1]
 * - Bit 4: Memory Attribute Index (MAIndex) [2]
 * - Bit 5: Non-secure (NS)
 * - Bit 6: AP[1] - Access Permission
 * - Bit 7: AP[2] - Access Permission
 * - Bit 8: Shareability [0]
 * - Bit 9: Shareability [1]
 * - Bit 10: Access Flag (AF)
 * - Bit 11: nG (not Global)
 */

/* 页表项类型 */
#define PTE_VALID           (1ULL << 0)    /* 有效位 */
#define PTE_TABLE           (1ULL << 1)    /* 表描述符 (L0-L2) */
#define PTE_BLOCK           (1ULL << 1)    /* 块描述符 (L1-L2) */
#define PTE_PAGE            (1ULL << 1)    /* 页描述符 (L3) */

/* 访问权限 (AP) */
#define PTE_AP_RW_EL0       (0ULL << 6)    /* RW, EL0/EL1 可访问 */
#define PTE_AP_RW_EL1       (1ULL << 6)    /* RW, 仅 EL1 可访问 */
#define PTE_AP_RO_EL0       (2ULL << 6)    /* RO, EL0/EL1 可访问 */
#define PTE_AP_RO_EL1       (3ULL << 6)    /* RO, 仅 EL1 可访问 */

/* 便捷宏 */
#define PTE_RW              PTE_AP_RW_EL1  /* 读写，仅内核 */
#define PTE_RO              PTE_AP_RO_EL1  /* 只读，仅内核 */
#define PTE_USER_RW         PTE_AP_RW_EL0  /* 读写，用户/内核 */
#define PTE_USER_RO         PTE_AP_RO_EL0  /* 只读，用户/内核 */

/* 共享属性 (SH) */
#define PTE_SH_NON_SHAREABLE    (0ULL << 8)    /* 非共享 */
#define PTE_SH_OUTER_SHAREABLE  (2ULL << 8)    /* 外部共享 */
#define PTE_SH_INNER_SHAREABLE  (3ULL << 8)    /* 内部共享 */

/* 访问标志 (AF) */
#define PTE_AF              (1ULL << 10)   /* 已访问标志 */

/* 全局/非全局 (nG) */
#define PTE_GLOBAL          (0ULL << 11)   /* 全局 TLB 条目 */
#define PTE_NG              (1ULL << 11)   /* 非全局 TLB 条目 (进程私有) */

/* 执行权限 (PXN, XN) */
#define PTE_PXN             (1ULL << 53)   /* 特权级执行禁止 */
#define PTE_XN              (1ULL << 54)   /* 执行禁止 */

/* 安全属性 */
#define PTE_NS              (1ULL << 5)    /* 非安全位 */

/* 地址掩码 - 提取物理地址 */
#define PTE_ADDR_MASK       (0x0000FFFFFFFFF000ULL)  /* L3 页表项地址掩码 */
#define PTE_BLOCK_ADDR_MASK (0x0000FFFFFFFFF000ULL)  /* 块描述符地址掩码 */

/*============================================================================
 *                              内存属性索引 (MAIR)
 *============================================================================*/

/**
 * @brief MAIR_EL1 属性索引
 * 
 * MAIR_EL1 寄存器定义了8个内存属性，通过 AttrIndex 选择
 */

/* 内存属性索引 */
#define MT_DEVICE_nGnRnE    0   /* Device-nGnRnE: 无聚合、无重排序、无早期写确认 */
#define MT_DEVICE_nGnRE     1   /* Device-nGnRE: 无聚合、无重排序、早期写确认 */
#define MT_DEVICE_GRE       2   /* Device-GRE: 聚合、重排序、早期写确认 */
#define MT_NORMAL_NC        3   /* Normal Memory, Non-Cacheable */
#define MT_NORMAL           4   /* Normal Memory, Cacheable */

/* MAIR_EL1 属性值 */
#define MAIR_ATTR_DEVICE_nGnRnE 0x00UL
#define MAIR_ATTR_DEVICE_nGnRE  0x04UL
#define MAIR_ATTR_DEVICE_GRE    0x0CUL
#define MAIR_ATTR_NORMAL_NC     0x44UL
#define MAIR_ATTR_NORMAL        0xFFUL

/* MAIR_EL1 初始值 */
#define MAIR_EL1_INIT \
    ((MAIR_ATTR_DEVICE_nGnRnE << (MT_DEVICE_nGnRnE * 8)) | \
     (MAIR_ATTR_DEVICE_nGnRE  << (MT_DEVICE_nGnRE  * 8)) | \
     (MAIR_ATTR_DEVICE_GRE    << (MT_DEVICE_GRE    * 8)) | \
     (MAIR_ATTR_NORMAL_NC     << (MT_NORMAL_NC     * 8)) | \
     (MAIR_ATTR_NORMAL        << (MT_NORMAL        * 8)))

/* 属性索引在页表项中的位置 */
#define PTE_ATTR_INDEX(idx) ((uint64_t)(idx) << 2)

/*============================================================================
 *                              便捷页表属性组合
 *============================================================================*/

/* 内核代码段: 可读可执行，不可写 */
#define PTE_KERNEL_CODE     (PTE_VALID | PTE_PAGE | PTE_AF | PTE_RO | \
                             PTE_SH_INNER_SHAREABLE | PTE_ATTR_INDEX(MT_NORMAL))

/* 内核数据段: 可读可写，不可执行 */
#define PTE_KERNEL_DATA     (PTE_VALID | PTE_PAGE | PTE_AF | PTE_RW | \
                             PTE_SH_INNER_SHAREABLE | PTE_ATTR_INDEX(MT_NORMAL) | PTE_PXN | PTE_XN)

/* 设备内存: 不可缓存，不可执行 */
#define PTE_DEVICE          (PTE_VALID | PTE_PAGE | PTE_AF | PTE_RW | \
                             PTE_SH_OUTER_SHAREABLE | PTE_ATTR_INDEX(MT_DEVICE_nGnRE) | PTE_PXN | PTE_XN)

/* 用户代码: 可读可执行 */
#define PTE_USER_CODE       (PTE_VALID | PTE_PAGE | PTE_AF | PTE_USER_RO | \
                             PTE_SH_INNER_SHAREABLE | PTE_ATTR_INDEX(MT_NORMAL) | PTE_NG)

/* 用户数据: 可读可写 */
#define PTE_USER_DATA       (PTE_VALID | PTE_PAGE | PTE_AF | PTE_USER_RW | \
                             PTE_SH_INNER_SHAREABLE | PTE_ATTR_INDEX(MT_NORMAL) | PTE_PXN | PTE_XN | PTE_NG)

/*============================================================================
 *                              页表级别定义
 *============================================================================*/

/* ARMv8-A 4级页表 (4KB 页面, 48位虚拟地址) */
#define PT_LEVELS           4       /* 页表级数 */
#define PT_ENTRIES          512     /* 每级页表条目数 */
#define PT_SIZE             4096    /* 页表大小 */

/* 页表级别 */
#define PT_L0               0       /* 第0级 (PGD) */
#define PT_L1               1       /* 第1级 (PUD) */
#define PT_L2               2       /* 第2级 (PMD) */
#define PT_L3               3       /* 第3级 (PTE) */

/* 虚拟地址分解 */
#define VA_INDEX(va, level) (((uint64_t)(va) >> (12 + (3 - (level)) * 9)) & 0x1FF)
#define VA_L0_INDEX(va)     VA_INDEX(va, PT_L0)
#define VA_L1_INDEX(va)     VA_INDEX(va, PT_L1)
#define VA_L2_INDEX(va)     VA_INDEX(va, PT_L2)
#define VA_L3_INDEX(va)     VA_INDEX(va, PT_L3)

/* 各级别映射范围 */
#define L0_MAP_SIZE         (512UL * 512UL * 512UL * PAGE_SIZE)  /* 256TB */
#define L1_MAP_SIZE         (512UL * 512UL * PAGE_SIZE)          /* 512GB */
#define L2_MAP_SIZE         (512UL * PAGE_SIZE)                  /* 1GB */
#define L3_MAP_SIZE         PAGE_SIZE                            /* 4KB */

/*============================================================================
 *                              类型定义
 *============================================================================*/

/**
 * @brief 物理页帧号 (PFN)
 */
typedef uint64_t pfn_t;

/**
 * @brief 物理地址
 */
typedef uint64_t phys_addr_t;

/**
 * @brief 虚拟地址
 */
typedef uint64_t virt_addr_t;

/**
 * @brief 页表项
 */
typedef uint64_t pte_t;

/**
 * @brief 页表指针
 */
typedef pte_t* pgt_t;

/**
 * @brief 内存区域类型
 */
typedef enum {
    MEMORY_TYPE_NORMAL,     /* 普通内存 */
    MEMORY_TYPE_DEVICE,     /* 设备内存 */
    MEMORY_TYPE_RESERVED,   /* 保留内存 */
    MEMORY_TYPE_KERNEL,     /* 内核内存 */
    MEMORY_TYPE_USER,       /* 用户内存 */
} memory_type_t;

/**
 * @brief 内存区域描述符
 */
typedef struct mem_region {
    phys_addr_t         phys_start;     /* 物理起始地址 */
    phys_addr_t         phys_end;       /* 物理结束地址 */
    virt_addr_t         virt_start;     /* 虚拟起始地址 */
    virt_addr_t         virt_end;       /* 虚拟结束地址 */
    memory_type_t       type;           /* 内存类型 */
    uint64_t            flags;          /* 属性标志 */
    struct mem_region   *next;          /* 链表指针 */
} mem_region_t;

/**
 * @brief 内存统计信息
 */
typedef struct mem_stats {
    uint64_t    total_pages;        /* 总页数 */
    uint64_t    free_pages;         /* 空闲页数 */
    uint64_t    used_pages;         /* 已用页数 */
    uint64_t    reserved_pages;     /* 保留页数 */
    uint64_t    kernel_pages;       /* 内核占用页数 */
} mem_stats_t;

/*============================================================================
 *                              函数声明
 *============================================================================*/

/**
 * @brief 内存管理初始化入口
 * @return 0 成功, 负数失败
 */
int mm_init(void);

/**
 * @brief 获取内存统计信息
 * @param stats 统计信息结构体指针
 */
void mm_get_stats(mem_stats_t *stats);

/**
 * @brief 打印内存布局信息
 */
void mm_print_layout(void);

/**
 * @brief 检查地址是否在内核空间
 * @param va 虚拟地址
 * @return true 在内核空间
 */
static inline bool is_kernel_addr(virt_addr_t va) {
    return (va >= KERNEL_VIRT_BASE);
}

/**
 * @brief 检查地址是否页对齐
 * @param addr 地址
 * @return true 页对齐
 */
static inline bool is_page_aligned(uint64_t addr) {
    return IS_PAGE_ALIGNED(addr);
}

/**
 * @brief 向上对齐到页边界
 * @param addr 地址
 * @return 对齐后的地址
 */
static inline uint64_t page_align_up(uint64_t addr) {
    return PAGE_ALIGN(addr);
}

/**
 * @brief 向下对齐到页边界
 * @param addr 地址
 * @return 对齐后的地址
 */
static inline uint64_t page_align_down(uint64_t addr) {
    return (addr & PAGE_MASK);
}

/**
 * @brief 地址转页帧号
 * @param addr 物理地址
 * @return 页帧号
 */
static inline pfn_t phys_to_pfn(phys_addr_t addr) {
    return addr >> PAGE_SHIFT;
}

/**
 * @brief 页帧号转物理地址
 * @param pfn 页帧号
 * @return 物理地址
 */
static inline phys_addr_t pfn_to_phys(pfn_t pfn) {
    return pfn << PAGE_SHIFT;
}

/**
 * @brief 计算页数
 * @param size 字节数
 * @return 页数
 */
static inline uint64_t size_to_pages(uint64_t size) {
    return (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
}

#endif /* _MM_H */
