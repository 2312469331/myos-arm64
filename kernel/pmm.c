/**
 * @file pmm.c
 * @brief 物理内存管理器实现
 * 
 * 使用位图(bitmap)管理物理页帧的分配与释放。
 * 支持单页分配、多页连续分配。
 */

#include "pmm.h"
#include "io.h"

/*============================================================================
 *                              全局变量
 *============================================================================*/

/* 物理内存管理器实例 */
static pmm_manager_t pmm = {0};

/* 位图存储区 (静态分配) */
static uint64_t pmm_bitmap[PMM_BITMAP_SIZE];

/*============================================================================
 *                              内联辅助函数
 *============================================================================*/

/**
 * @brief 设置位图位 (标记已分配)
 */
static inline void bitmap_set(uint64_t *bitmap, uint64_t index) {
    uint64_t word = index / PMM_BITMAP_BITS;
    uint64_t bit = index % PMM_BITMAP_BITS;
    bitmap[word] |= (1ULL << bit);
}

/**
 * @brief 清除位图位 (标记空闲)
 */
static inline void bitmap_clear(uint64_t *bitmap, uint64_t index) {
    uint64_t word = index / PMM_BITMAP_BITS;
    uint64_t bit = index % PMM_BITMAP_BITS;
    bitmap[word] &= ~(1ULL << bit);
}

/**
 * @brief 测试位图位
 */
static inline bool bitmap_test(uint64_t *bitmap, uint64_t index) {
    uint64_t word = index / PMM_BITMAP_BITS;
    uint64_t bit = index % PMM_BITMAP_BITS;
    return (bitmap[word] & (1ULL << bit)) != 0;
}

/**
 * @brief 查找连续空闲位
 * @param bitmap 位图
 * @param start 起始索引
 * @param count 需要的连续位数
 * @return 找到的起始索引，-1表示未找到
 */
static inline int64_t bitmap_find_free(uint64_t *bitmap, uint64_t start, 
                                        uint64_t count) {
    uint64_t found = 0;
    uint64_t first_index = 0;
    
    for (uint64_t i = start; i < pmm.total_pages; i++) {
        if (!bitmap_test(bitmap, i)) {
            if (found == 0) {
                first_index = i;
            }
            found++;
            
            if (found == count) {
                return (int64_t)first_index;
            }
        } else {
            found = 0;
        }
    }
    
    return -1;
}

/**
 * @brief 获取自旋锁
 */
static inline void pmm_lock(void) {
    while (__atomic_test_and_set(&pmm.lock, __ATOMIC_ACQUIRE)) {
        /* 自旋等待 */
        while (pmm.lock) {
            __asm__ volatile ("yield" ::: "memory");
        }
    }
}

/**
 * @brief 释放自旋锁
 */
static inline void pmm_unlock(void) {
    __atomic_clear(&pmm.lock, __ATOMIC_RELEASE);
}

/*============================================================================
 *                              初始化函数
 *============================================================================*/

/**
 * @brief 初始化物理内存管理器
 */
int pmm_init(phys_addr_t mem_start, uint64_t mem_size) {
    /* 参数检查 */
    if (mem_size == 0 || mem_size > PMM_MAX_MEMORY) {
        return -1;
    }
    
    /* 页对齐 */
    mem_start = PAGE_ALIGN(mem_start);
    mem_size = PAGE_ALIGN(mem_size);
    
    /* 初始化管理器 */
    pmm.mem_start = mem_start;
    pmm.mem_end = mem_start + mem_size;
    pmm.total_pages = mem_size / PAGE_SIZE;
    pmm.free_pages = pmm.total_pages;
    pmm.reserved_pages = 0;
    pmm.kernel_pages = 0;
    
    /* 初始化位图 */
    pmm.bitmap = pmm_bitmap;
    pmm.bitmap_size = (pmm.total_pages + PMM_BITMAP_BITS - 1) / PMM_BITMAP_BITS;
    
    /* 清空位图 (全部标记为空闲) */
    memzero(pmm.bitmap, pmm.bitmap_size * sizeof(uint64_t));
    
    /* 初始化锁 */
    pmm.lock = 0;
    
    /* 初始化内存区域 */
    /* DMA区域: 0 - 16MB */
    pmm.zones[PMM_ZONE_DMA].name = "DMA";
    pmm.zones[PMM_ZONE_DMA].start = mem_start;
    pmm.zones[PMM_ZONE_DMA].end = mem_start + PMM_ZONE_DMA_END;
    if (pmm.zones[PMM_ZONE_DMA].end > pmm.mem_end) {
        pmm.zones[PMM_ZONE_DMA].end = pmm.mem_end;
    }
    pmm.zones[PMM_ZONE_DMA].total_pages = 
        (pmm.zones[PMM_ZONE_DMA].end - pmm.zones[PMM_ZONE_DMA].start) / PAGE_SIZE;
    pmm.zones[PMM_ZONE_DMA].free_pages = pmm.zones[PMM_ZONE_DMA].total_pages;
    pmm.zones[PMM_ZONE_DMA].bitmap = pmm.bitmap;
    pmm.zones[PMM_ZONE_DMA].first_free = 0;
    
    /* 普通区域: 16MB - 结束 */
    pmm.zones[PMM_ZONE_NORMAL].name = "Normal";
    pmm.zones[PMM_ZONE_NORMAL].start = pmm.zones[PMM_ZONE_DMA].end;
    pmm.zones[PMM_ZONE_NORMAL].end = pmm.mem_end;
    pmm.zones[PMM_ZONE_NORMAL].total_pages = 
        (pmm.zones[PMM_ZONE_NORMAL].end - pmm.zones[PMM_ZONE_NORMAL].start) / PAGE_SIZE;
    pmm.zones[PMM_ZONE_NORMAL].free_pages = pmm.zones[PMM_ZONE_NORMAL].total_pages;
    pmm.zones[PMM_ZONE_NORMAL].bitmap = pmm.bitmap;
    pmm.zones[PMM_ZONE_NORMAL].first_free = pmm.zones[PMM_ZONE_DMA].total_pages;
    
    return 0;
}

/**
 * @brief 添加可用内存区域
 */
int pmm_add_region(phys_addr_t start, uint64_t size) {
    /* 页对齐 */
    start = PAGE_ALIGN(start);
    size = PAGE_ALIGN(size);
    
    /* 边界检查 */
    if (start < pmm.mem_start || start + size > pmm.mem_end) {
        return -1;
    }
    
    pfn_t start_pfn = phys_to_pfn(start - pmm.mem_start);
    pfn_t end_pfn = start_pfn + size / PAGE_SIZE;
    
    pmm_lock();
    
    /* 清除位图，标记为可用 */
    for (pfn_t pfn = start_pfn; pfn < end_pfn; pfn++) {
        if (bitmap_test(pmm.bitmap, pfn)) {
            bitmap_clear(pmm.bitmap, pfn);
            pmm.free_pages++;
        }
    }
    
    pmm_unlock();
    
    return 0;
}

/**
 * @brief 保留内存区域
 */
int pmm_reserve_region(phys_addr_t start, uint64_t size) {
    /* 页对齐 */
    start = page_align_down(start);
    size = PAGE_ALIGN(size);
    
    /* 边界检查 */
    if (start < pmm.mem_start || start + size > pmm.mem_end) {
        return -1;
    }
    
    pfn_t start_pfn = phys_to_pfn(start - pmm.mem_start);
    pfn_t end_pfn = start_pfn + size / PAGE_SIZE;
    
    pmm_lock();
    
    /* 设置位图，标记为已分配 */
    for (pfn_t pfn = start_pfn; pfn < end_pfn; pfn++) {
        if (!bitmap_test(pmm.bitmap, pfn)) {
            bitmap_set(pmm.bitmap, pfn);
            pmm.free_pages--;
            pmm.reserved_pages++;
        }
    }
    
    pmm_unlock();
    
    return 0;
}

/*============================================================================
 *                              分配函数
 *============================================================================*/

/**
 * @brief 分配单个物理页
 */
phys_addr_t pmm_alloc_page(uint32_t flags) {
    phys_addr_t pa = 0;
    
    pmm_lock();
    
    /* 查找空闲页 */
    int64_t index = bitmap_find_free(pmm.bitmap, 0, 1);
    if (index < 0) {
        pmm_unlock();
        return 0;  /* 无可用内存 */
    }
    
    /* 标记为已分配 */
    bitmap_set(pmm.bitmap, (uint64_t)index);
    pmm.free_pages--;
    
    /* 计算物理地址 */
    pa = pmm.mem_start + ((uint64_t)index << PAGE_SHIFT);
    
    pmm_unlock();
    
    /* 如果需要清零 */
    if (flags & PMM_FLAG_ZERO) {
        void *va = PHYS_TO_VIRT(pa);
        memzero(va, PAGE_SIZE);
    }
    
    return pa;
}

/**
 * @brief 分配多个连续物理页
 */
phys_addr_t pmm_alloc_pages(uint64_t count, uint32_t flags) {
    if (count == 0) {
        return 0;
    }
    
    if (count == 1) {
        return pmm_alloc_page(flags);
    }
    
    phys_addr_t pa = 0;
    
    pmm_lock();
    
    /* 查找连续空闲页 */
    int64_t index = bitmap_find_free(pmm.bitmap, 0, count);
    if (index < 0) {
        pmm_unlock();
        return 0;  /* 无足够连续内存 */
    }
    
    /* 标记为已分配 */
    for (uint64_t i = 0; i < count; i++) {
        bitmap_set(pmm.bitmap, (uint64_t)index + i);
    }
    pmm.free_pages -= count;
    
    /* 计算物理地址 */
    pa = pmm.mem_start + ((uint64_t)index << PAGE_SHIFT);
    
    pmm_unlock();
    
    /* 如果需要清零 */
    if (flags & PMM_FLAG_ZERO) {
        void *va = PHYS_TO_VIRT(pa);
        memzero(va, count * PAGE_SIZE);
    }
    
    return pa;
}

/**
 * @brief 在指定地址分配物理页
 */
int pmm_alloc_at(phys_addr_t pa, uint32_t flags) {
    /* 检查对齐 */
    if (!IS_PAGE_ALIGNED(pa)) {
        return -1;
    }
    
    /* 边界检查 */
    if (pa < pmm.mem_start || pa >= pmm.mem_end) {
        return -1;
    }
    
    pfn_t pfn = phys_to_pfn(pa - pmm.mem_start);
    
    pmm_lock();
    
    /* 检查是否已分配 */
    if (bitmap_test(pmm.bitmap, pfn)) {
        pmm_unlock();
        return -1;  /* 已被分配 */
    }
    
    /* 标记为已分配 */
    bitmap_set(pmm.bitmap, pfn);
    pmm.free_pages--;
    
    pmm_unlock();
    
    /* 如果需要清零 */
    if (flags & PMM_FLAG_ZERO) {
        void *va = PHYS_TO_VIRT(pa);
        memzero(va, PAGE_SIZE);
    }
    
    return 0;
}

/*============================================================================
 *                              释放函数
 *============================================================================*/

/**
 * @brief 释放单个物理页
 */
void pmm_free_page(phys_addr_t pa) {
    /* 检查对齐 */
    if (!IS_PAGE_ALIGNED(pa)) {
        return;
    }
    
    /* 边界检查 */
    if (pa < pmm.mem_start || pa >= pmm.mem_end) {
        return;
    }
    
    pfn_t pfn = phys_to_pfn(pa - pmm.mem_start);
    
    pmm_lock();
    
    /* 检查是否已分配 */
    if (!bitmap_test(pmm.bitmap, pfn)) {
        pmm_unlock();
        return;  /* 未被分配，无需释放 */
    }
    
    /* 清除位图，标记为空闲 */
    bitmap_clear(pmm.bitmap, pfn);
    pmm.free_pages++;
    
    /* 更新first_free */
    if (pfn < pmm.zones[PMM_ZONE_NORMAL].first_free) {
        pmm.zones[PMM_ZONE_NORMAL].first_free = pfn;
    }
    
    pmm_unlock();
}

/**
 * @brief 释放多个连续物理页
 */
void pmm_free_pages(phys_addr_t pa, uint64_t count) {
    if (count == 0) {
        return;
    }
    
    if (count == 1) {
        pmm_free_page(pa);
        return;
    }
    
    /* 检查对齐 */
    if (!IS_PAGE_ALIGNED(pa)) {
        return;
    }
    
    /* 边界检查 */
    if (pa < pmm.mem_start || pa + count * PAGE_SIZE > pmm.mem_end) {
        return;
    }
    
    pfn_t start_pfn = phys_to_pfn(pa - pmm.mem_start);
    
    pmm_lock();
    
    /* 清除位图 */
    for (uint64_t i = 0; i < count; i++) {
        pfn_t pfn = start_pfn + i;
        if (bitmap_test(pmm.bitmap, pfn)) {
            bitmap_clear(pmm.bitmap, pfn);
            pmm.free_pages++;
        }
    }
    
    /* 更新first_free */
    if (start_pfn < pmm.zones[PMM_ZONE_NORMAL].first_free) {
        pmm.zones[PMM_ZONE_NORMAL].first_free = start_pfn;
    }
    
    pmm_unlock();
}

/*============================================================================
 *                              查询函数
 *============================================================================*/

/**
 * @brief 检查物理页是否已分配
 */
bool pmm_is_allocated(phys_addr_t pa) {
    /* 边界检查 */
    if (pa < pmm.mem_start || pa >= pmm.mem_end) {
        return false;
    }
    
    pfn_t pfn = phys_to_pfn(pa - pmm.mem_start);
    
    pmm_lock();
    bool result = bitmap_test(pmm.bitmap, pfn);
    pmm_unlock();
    
    return result;
}

/**
 * @brief 获取空闲页数
 */
uint64_t pmm_get_free_pages(void) {
    return pmm.free_pages;
}

/**
 * @brief 获取总页数
 */
uint64_t pmm_get_total_pages(void) {
    return pmm.total_pages;
}

/**
 * @brief 获取物理内存管理器统计信息
 */
void pmm_get_stats(mem_stats_t *stats) {
    if (!stats) {
        return;
    }
    
    stats->total_pages = pmm.total_pages;
    stats->free_pages = pmm.free_pages;
    stats->used_pages = pmm.total_pages - pmm.free_pages - pmm.reserved_pages;
    stats->reserved_pages = pmm.reserved_pages;
    stats->kernel_pages = pmm.kernel_pages;
}

/**
 * @brief 打印物理内存信息
 */
void pmm_print_info(void) {
    /* 实际应使用printk */
    /*
    printk("PMM: Physical Memory Manager\n");
    printk("  Memory Range: 0x%lx - 0x%lx\n", pmm.mem_start, pmm.mem_end);
    printk("  Total Pages:  %lu (%lu MB)\n", 
           pmm.total_pages, 
           pmm.total_pages * PAGE_SIZE / (1024 * 1024));
    printk("  Free Pages:   %lu (%lu MB)\n", 
           pmm.free_pages, 
           pmm.free_pages * PAGE_SIZE / (1024 * 1024));
    printk("  Reserved:     %lu pages\n", pmm.reserved_pages);
    */
}
