/**
 * @file pmm.h
 * @brief 物理内存管理器 (Physical Memory Manager)
 *
 * 实现物理页帧的分配与释放，使用位图(bitmap)管理物理内存。
 * 支持单页分配、多页连续分配以及大块内存分配。
 */

#ifndef _PMM_H
#define _PMM_H

#include "mm.h"

/*============================================================================
 *                              配置常量
 *============================================================================*/

/* 位图管理最大内存 (4GB) */
#define PMM_MAX_MEMORY (4UL * 1024 * 1024 * 1024)

/* 最大物理页数 */
#define PMM_MAX_PAGES (PMM_MAX_MEMORY / PAGE_SIZE)

/* 位图每个元素管理的页数 (64位) */
#define PMM_BITMAP_BITS 64

/* 位图数组大小 */
#define PMM_BITMAP_SIZE (PMM_MAX_PAGES / PMM_BITMAP_BITS)

/* 默认分配标志 */
#define PMM_FLAG_NONE 0x0000
#define PMM_FLAG_ZERO 0x0001       /* 分配后清零 */
#define PMM_FLAG_DMA 0x0002        /* DMA 可用内存 (低地址) */
#define PMM_FLAG_CONTIGUOUS 0x0004 /* 连续物理页 */

/* 物理内存区域 */
#define PMM_ZONE_DMA 0    /* DMA 区域 (0-16MB) */
#define PMM_ZONE_NORMAL 1 /* 普通区域 */
#define PMM_ZONE_HIGH 2   /* 高端内存区域 */
#define PMM_ZONE_COUNT 3

/* 区域边界 */
#define PMM_ZONE_DMA_END (16UL * 1024 * 1024) /* DMA 区域结束: 16MB */

/*============================================================================
 *                              数据结构
 *============================================================================*/

/**
 * @brief 物理内存区域描述符
 */
typedef struct pmm_zone {
  const char *name;     /* 区域名称 */
  phys_addr_t start;    /* 起始物理地址 */
  phys_addr_t end;      /* 结束物理地址 */
  uint64_t total_pages; /* 总页数 */
  uint64_t free_pages;  /* 空闲页数 */
  uint64_t *bitmap;     /* 位图指针 */
  uint64_t bitmap_size; /* 位图大小 (元素数) */
  uint64_t first_free;  /* 第一个空闲页索引 */
} pmm_zone_t;

/**
 * @brief 物理内存管理器
 */
typedef struct pmm_manager {
  /* 全局信息 */
  phys_addr_t mem_start;   /* 物理内存起始地址 */
  phys_addr_t mem_end;     /* 物理内存结束地址 */
  uint64_t total_pages;    /* 总物理页数 */
  uint64_t free_pages;     /* 空闲页数 */
  uint64_t reserved_pages; /* 保留页数 */
  uint64_t kernel_pages;   /* 内核占用页数 */

  /* 位图管理 */
  uint64_t *bitmap;     /* 全局位图 */
  uint64_t bitmap_size; /* 位图大小 */

  /* 区域管理 */
  pmm_zone_t zones[PMM_ZONE_COUNT]; /* 内存区域 */

  /* 锁 (用于SMP) */
  volatile uint32_t lock; /* 自旋锁 */
} pmm_manager_t;

/**
 * @brief 物理页信息结构
 */
typedef struct phys_page {
  pfn_t pfn;          /* 页帧号 */
  uint32_t ref_count; /* 引用计数 */
  uint32_t flags;     /* 页标志 */
  uint64_t private;   /* 私有数据 */
} phys_page_t;

/* 页标志 */
#define PAGE_FLAG_RESERVED (1 << 0) /* 保留页 */
#define PAGE_FLAG_KERNEL (1 << 1)   /* 内核页 */
#define PAGE_FLAG_DMA (1 << 2)      /* DMA 页 */
#define PAGE_FLAG_DIRTY (1 << 3)    /* 脏页 */
#define PAGE_FLAG_LOCKED (1 << 4)   /* 锁定页 */

/*============================================================================
 *                              函数声明
 *============================================================================*/

/**
 * @brief 初始化物理内存管理器
 * @param mem_start 物理内存起始地址
 * @param mem_size 物理内存大小 (字节)
 * @return 0 成功, 负数失败
 */
int pmm_init(phys_addr_t mem_start, uint64_t mem_size);

/**
 * @brief 添加可用内存区域
 * @param start 起始物理地址
 * @param size 区域大小
 * @return 0 成功, 负数失败
 */
int pmm_add_region(phys_addr_t start, uint64_t size);

/**
 * @brief 保留内存区域 (不可分配)
 * @param start 起始物理地址
 * @param size 区域大小
 * @return 0 成功, 负数失败
 */
int pmm_reserve_region(phys_addr_t start, uint64_t size);

/**
 * @brief 分配单个物理页
 * @param flags 分配标志
 * @return 物理地址, 0 表示失败
 */
phys_addr_t pmm_alloc_page(uint32_t flags);

/**
 * @brief 分配多个连续物理页
 * @param count 页数
 * @param flags 分配标志
 * @return 起始物理地址, 0 表示失败
 */
phys_addr_t pmm_alloc_pages(uint64_t count, uint32_t flags);

/**
 * @brief 在指定地址分配物理页
 * @param pa 指定的物理地址
 * @param flags 分配标志
 * @return 0 成功, 负数失败
 */
int pmm_alloc_at(phys_addr_t pa, uint32_t flags);

/**
 * @brief 释放单个物理页
 * @param pa 物理地址
 */
void pmm_free_page(phys_addr_t pa);

/**
 * @brief 释放多个连续物理页
 * @param pa 起始物理地址
 * @param count 页数
 */
void pmm_free_pages(phys_addr_t pa, uint64_t count);

/**
 * @brief 检查物理页是否已分配
 * @param pa 物理地址
 * @return true 已分配
 */
bool pmm_is_allocated(phys_addr_t pa);

/**
 * @brief 获取空闲页数
 * @return 空闲页数
 */
uint64_t pmm_get_free_pages(void);

/**
 * @brief 获取总页数
 * @return 总页数
 */
uint64_t pmm_get_total_pages(void);

/**
 * @brief 获取物理内存管理器统计信息
 * @param stats 统计信息结构体指针
 */
void pmm_get_stats(mem_stats_t *stats);

/**
 * @brief 打印物理内存信息
 */
void pmm_print_info(void);

/*============================================================================
 *                              内联辅助函数
 *============================================================================*/

/**
 * @brief 检查地址是否在DMA区域
 */
static inline bool pmm_is_dma_addr(phys_addr_t pa) {
  return pa < PMM_ZONE_DMA_END;
}

/**
 * @brief 获取物理地址所在的内存区域
 */
static inline int pmm_get_zone(phys_addr_t pa) {
  if (pa < PMM_ZONE_DMA_END) {
    return PMM_ZONE_DMA;
  }
  return PMM_ZONE_NORMAL;
}

/**
 * @brief 物理地址转页帧号
 */
static inline pfn_t pmm_phys_to_pfn(phys_addr_t pa) { return phys_to_pfn(pa); }

/**
 * @brief 页帧号转物理地址
 */
static inline phys_addr_t pmm_pfn_to_phys(pfn_t pfn) {
  return pfn_to_phys(pfn);
}

/**
 * @brief 分配物理页并清零
 */
static inline phys_addr_t pmm_alloc_page_zero(void) {
  return pmm_alloc_page(PMM_FLAG_ZERO);
}

/**
 * @brief 分配DMA可用物理页
 */
static inline phys_addr_t pmm_alloc_dma_page(void) {
  return pmm_alloc_page(PMM_FLAG_DMA);
}

#endif /* _PMM_H */
