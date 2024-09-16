/**
 * @file io.c
 * @brief ARM64 I/O 操作实现
 * 
 * 提供内存操作、DMA操作、定时器等底层功能的实现。
 */

#include "io.h"
#include "pmm.h"
#include "vmm.h"

/*============================================================================
 *                              内存操作函数
 *============================================================================*/

/**
 * @brief 内存复制
 */
void* memcpy(void *dest, const void *src, uint64_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    
    /* 64位对齐复制 */
    while (n >= 8) {
        *(uint64_t *)d = *(const uint64_t *)s;
        d += 8;
        s += 8;
        n -= 8;
    }
    
    /* 32位复制 */
    while (n >= 4) {
        *(uint32_t *)d = *(const uint32_t *)s;
        d += 4;
        s += 4;
        n -= 4;
    }
    
    /* 字节复制 */
    while (n--) {
        *d++ = *s++;
    }
    
    return dest;
}

/**
 * @brief 内存移动 (处理重叠)
 */
void* memmove(void *dest, const void *src, uint64_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    
    if (d < s) {
        /* 向前复制 */
        while (n--) {
            *d++ = *s++;
        }
    } else if (d > s) {
        /* 向后复制 */
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    
    return dest;
}

/**
 * @brief 内存设置
 */
void* memset(void *s, int c, uint64_t n) {
    uint8_t *p = (uint8_t *)s;
    
    /* 优化: 一次设置8字节 */
    if (n >= 8) {
        uint64_t pattern = (uint8_t)c;
        pattern |= pattern << 8;
        pattern |= pattern << 16;
        pattern |= pattern << 32;
        
        while (n >= 8) {
            *(uint64_t *)p = pattern;
            p += 8;
            n -= 8;
        }
    }
    
    while (n--) {
        *p++ = (uint8_t)c;
    }
    
    return s;
}

/**
 * @brief 内存清零
 */
void memzero(void *s, uint64_t n) {
    memset(s, 0, n);
}

/**
 * @brief 内存比较
 */
int memcmp(const void *s1, const void *s2, uint64_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    
    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    
    return 0;
}

/**
 * @brief 字符串长度
 */
uint64_t strlen(const char *s) {
    const char *p = s;
    while (*p) {
        p++;
    }
    return p - s;
}

/**
 * @brief 字符串复制
 */
char* strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

/**
 * @brief 字符串比较
 */
int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *(const uint8_t *)s1 - *(const uint8_t *)s2;
}

/*============================================================================
 *                              DMA操作函数
 *============================================================================*/

/**
 * @brief DMA缓冲区描述符
 */
typedef struct dma_buffer {
    phys_addr_t     phys;           /* 物理地址 */
    void            *virt;          /* 虚拟地址 */
    uint64_t        size;           /* 大小 */
    uint32_t        flags;          /* 标志 */
    struct dma_buffer *next;        /* 链表 */
} dma_buffer_t;

/* DMA缓冲区链表 */
static dma_buffer_t *dma_buffers = NULL;
static volatile uint32_t dma_lock = 0;

/**
 * @brief 分配DMA缓冲区
 */
void* dma_alloc_coherent(uint64_t size, phys_addr_t *dma_handle, uint32_t flags) {
    if (size == 0 || !dma_handle) {
        return NULL;
    }
    
    /* 对齐到页面 */
    size = PAGE_ALIGN(size);
    
    /* 分配物理连续内存 */
    phys_addr_t pa = pmm_alloc_pages(size / PAGE_SIZE, PMM_FLAG_ZERO);
    if (pa == 0) {
        return NULL;
    }
    
    /* 映射到内核虚拟地址空间 (uncached) */
    virt_addr_t va = vmm_alloc_kernel_virt(size);
    if (va == 0) {
        pmm_free_pages(pa, size / PAGE_SIZE);
        return NULL;
    }
    
    /* 使用设备内存属性映射 (non-cacheable) */
    uint64_t pte_flags = PTE_VALID | PTE_PAGE | PTE_AF | 
                         PTE_ATTR_INDEX(MT_NORMAL_NC) |
                         PTE_RW | PTE_PXN | PTE_XN;
    
    if (vmm_map_range(vmm_get_current_pgt(), va, pa, size, pte_flags) < 0) {
        pmm_free_pages(pa, size / PAGE_SIZE);
        return NULL;
    }
    
    /* 创建DMA缓冲区描述符 */
    dma_buffer_t *buf = kzalloc(sizeof(dma_buffer_t), GFP_KERNEL);
    if (!buf) {
        vmm_unmap_range(vmm_get_current_pgt(), va, size);
        pmm_free_pages(pa, size / PAGE_SIZE);
        return NULL;
    }
    
    buf->phys = pa;
    buf->virt = (void *)va;
    buf->size = size;
    buf->flags = flags;
    
    /* 添加到链表 */
    while (__atomic_test_and_set(&dma_lock, __ATOMIC_ACQUIRE)) {
        __asm__ volatile ("yield" ::: "memory");
    }
    
    buf->next = dma_buffers;
    dma_buffers = buf;
    
    __atomic_clear(&dma_lock, __ATOMIC_RELEASE);
    
    *dma_handle = pa;
    return (void *)va;
}

/**
 * @brief 释放DMA缓冲区
 */
void dma_free_coherent(void *vaddr, uint64_t size, phys_addr_t dma_handle) {
    if (!vaddr || size == 0) {
        return;
    }
    
    size = PAGE_ALIGN(size);
    
    /* 从链表中查找并移除 */
    while (__atomic_test_and_set(&dma_lock, __ATOMIC_ACQUIRE)) {
        __asm__ volatile ("yield" ::: "memory");
    }
    
    dma_buffer_t **pp = &dma_buffers;
    while (*pp) {
        if ((*pp)->virt == vaddr) {
            dma_buffer_t *buf = *pp;
            *pp = buf->next;
            
            __atomic_clear(&dma_lock, __ATOMIC_RELEASE);
            
            /* 取消映射 */
            vmm_unmap_range(vmm_get_current_pgt(), 
                           (virt_addr_t)buf->virt, buf->size);
            
            /* 释放物理内存 */
            pmm_free_pages(buf->phys, buf->size / PAGE_SIZE);
            
            /* 释放描述符 */
            kfree(buf);
            return;
        }
        pp = &(*pp)->next;
    }
    
    __atomic_clear(&dma_lock, __ATOMIC_RELEASE);
}

/**
 * @brief 映射单缓冲区用于DMA
 */
phys_addr_t dma_map_single(void *vaddr, uint64_t size, 
                           enum dma_data_direction dir) {
    if (!vaddr || size == 0) {
        return 0;
    }
    
    /* 获取物理地址 */
    phys_addr_t pa;
    if (vmm_query_mapping(vmm_get_current_pgt(), 
                          (virt_addr_t)vaddr, &pa, NULL) < 0) {
        return 0;
    }
    
    /* 根据方向执行缓存操作 */
    switch (dir) {
    case DMA_TO_DEVICE:
        /* 清除缓存，确保数据写入内存 */
        cache_clean(vaddr, size);
        break;
        
    case DMA_FROM_DEVICE:
        /* 使缓存无效，确保从内存读取 */
        cache_invalidate(vaddr, size);
        break;
        
    case DMA_BIDIRECTIONAL:
        /* 双向: 清除并使无效 */
        cache_clean_invalidate(vaddr, size);
        break;
    }
    
    return pa;
}

/**
 * @brief 取消映射DMA缓冲区
 */
void dma_unmap_single(phys_addr_t dma_handle, uint64_t size, 
                      enum dma_data_direction dir) {
    if (dma_handle == 0 || size == 0) {
        return;
    }
    
    /* 根据方向执行缓存操作 */
    if (dir == DMA_FROM_DEVICE || dir == DMA_BIDIRECTIONAL) {
        /* 使缓存无效，确保CPU看到DMA写入的数据 */
        /* 需要虚拟地址，这里简化处理 */
    }
}

/*============================================================================
 *                              缓存操作函数
 *============================================================================*/

/**
 * @brief 清除数据缓存到指定范围
 */
void cache_clean(void *vaddr, uint64_t size) {
    virt_addr_t va = (virt_addr_t)vaddr;
    virt_addr_t end = va + size;
    
    /* 按缓存行对齐 */
    va = va & ~(CACHE_LINE_SIZE - 1);
    
    while (va < end) {
        __asm__ volatile (
            "dc cvac, %0"   /* Clean by VA to PoC */
            :: "r" (va)
            : "memory"
        );
        va += CACHE_LINE_SIZE;
    }
    
    dsb();
}

/**
 * @brief 使数据缓存无效
 */
void cache_invalidate(void *vaddr, uint64_t size) {
    virt_addr_t va = (virt_addr_t)vaddr;
    virt_addr_t end = va + size;
    
    va = va & ~(CACHE_LINE_SIZE - 1);
    
    while (va < end) {
        __asm__ volatile (
            "dc ivac, %0"   /* Invalidate by VA to PoC */
            :: "r" (va)
            : "memory"
        );
        va += CACHE_LINE_SIZE;
    }
    
    dsb();
}

/**
 * @brief 清除并使数据缓存无效
 */
void cache_clean_invalidate(void *vaddr, uint64_t size) {
    virt_addr_t va = (virt_addr_t)vaddr;
    virt_addr_t end = va + size;
    
    va = va & ~(CACHE_LINE_SIZE - 1);
    
    while (va < end) {
        __asm__ volatile (
            "dc civac, %0"  /* Clean and Invalidate by VA to PoC */
            :: "r" (va)
            : "memory"
        );
        va += CACHE_LINE_SIZE;
    }
    
    dsb();
}

/**
 * @brief 清除整个数据缓存
 */
void cache_clean_all(void) {
    /* 使用系统寄存器遍历所有缓存行 */
    /* 简化实现: 使用set/way操作 */
    uint64_t ccsidr;
    uint64_t sets, ways;
    
    /* 读取CCSIDR获取缓存几何信息 */
    __asm__ volatile (
        "mrs %0, CCSIDR_EL1"
        : "=r" (ccsidr)
    );
    
    sets = ((ccsidr >> 13) & 0x7FFF) + 1;
    ways = ((ccsidr >> 3) & 0x3FF) + 1;
    
    /* 按set/way清除 */
    for (uint64_t way = 0; way < ways; way++) {
        for (uint64_t set = 0; set < sets; set++) {
            __asm__ volatile (
                "dc cisw, %0"
                :: "r" ((way << 30) | (set << 5))
                : "memory"
            );
        }
    }
    
    dsb();
}

/**
 * @brief 使整个数据缓存无效
 */
void cache_invalidate_all(void) {
    uint64_t ccsidr;
    uint64_t sets, ways;
    
    __asm__ volatile (
        "mrs %0, CCSIDR_EL1"
        : "=r" (ccsidr)
    );
    
    sets = ((ccsidr >> 13) & 0x7FFF) + 1;
    ways = ((ccsidr >> 3) & 0x3FF) + 1;
    
    for (uint64_t way = 0; way < ways; way++) {
        for (uint64_t set = 0; set < sets; set++) {
            __asm__ volatile (
                "dc isw, %0"
                :: "r" ((way << 30) | (set << 5))
                : "memory"
            );
        }
    }
    
    dsb();
}

/*============================================================================
 *                              定时器操作函数
 *============================================================================*/

/**
 * @brief 获取系统计数器值
 */
uint64_t timer_get_counter(void) {
    uint64_t cnt;
    __asm__ volatile ("mrs %0, CNTVCT_EL0" : "=r" (cnt));
    return cnt;
}

/**
 * @brief 获取定时器频率
 */
uint64_t timer_get_frequency(void) {
    uint64_t freq;
    __asm__ volatile ("mrs %0, CNTFRQ_EL0" : "=r" (freq));
    return freq;
}

/**
 * @brief 微秒延迟
 */
void udelay(uint64_t us) {
    uint64_t freq = timer_get_frequency();
    uint64_t count = (freq * us) / 1000000;
    uint64_t start = timer_get_counter();
    
    while ((timer_get_counter() - start) < count) {
        __asm__ volatile ("yield" ::: "memory");
    }
}

/**
 * @brief 毫秒延迟
 */
void mdelay(uint64_t ms) {
    udelay(ms * 1000);
}

/**
 * @brief 秒延迟
 */
void delay(uint64_t s) {
    mdelay(s * 1000);
}

/*============================================================================
 *                              系统控制函数
 *============================================================================*/

/**
 * @brief 系统重启
 */
void system_reboot(void) {
    /* 等待所有操作完成 */
    dsb();
    
    /* PSCI调用重启 */
    /* 这里使用SMC调用 */
    __asm__ volatile (
        "mov x0, #0x84000009\n"  /* PSCI_SYSTEM_RESET */
        "smc #0\n"
        ::: "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "memory"
    );
    
    /* 不应该到达这里 */
    while (1) {
        __asm__ volatile ("wfi");
    }
}

/**
 * @brief 系统关机
 */
void system_shutdown(void) {
    dsb();
    
    /* PSCI调用关机 */
    __asm__ volatile (
        "mov x0, #0x84000008\n"  /* PSCI_SYSTEM_OFF */
        "smc #0\n"
        ::: "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "memory"
    );
    
    while (1) {
        __asm__ volatile ("wfi");
    }
}

/*============================================================================
 *                              中断控制函数
 *============================================================================*/

/**
 * @brief 使能中断
 */
void enable_irq(void) {
    __asm__ volatile ("msr DAIFClr, #2" ::: "memory");
}

/**
 * @brief 禁止中断
 */
void disable_irq(void) {
    __asm__ volatile ("msr DAIFSet, #2" ::: "memory");
}

/**
 * @brief 使能所有中断
 */
void enable_interrupts(void) {
    __asm__ volatile ("msr DAIFClr, #0xF" ::: "memory");
}

/**
 * @brief 禁止所有中断
 */
void disable_interrupts(void) {
    __asm__ volatile ("msr DAIFSet, #0xF" ::: "memory");
}

/**
 * @brief 保存中断状态并禁止
 */
uint64_t save_interrupts(void) {
    uint64_t daif;
    __asm__ volatile (
        "mrs %0, DAIF\n"
        "msr DAIFSet, #0xF"
        : "=r" (daif)
        ::: "memory"
    );
    return daif;
}

/**
 * @brief 恢复中断状态
 */
void restore_interrupts(uint64_t state) {
    __asm__ volatile (
        "msr DAIF, %0"
        :: "r" (state)
        : "memory"
    );
}

/*============================================================================
 *                              自旋锁实现
 *============================================================================*/

/**
 * @brief 初始化自旋锁
 */
void spinlock_init(spinlock_t *lock) {
    if (lock) {
        lock->locked = 0;
        lock->irq_saved = 0;
    }
}

/**
 * @brief 获取自旋锁
 */
void spinlock_acquire(spinlock_t *lock) {
    if (!lock) {
        return;
    }
    
    while (__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE)) {
        while (lock->locked) {
            __asm__ volatile ("yield" ::: "memory");
        }
    }
}

/**
 * @brief 释放自旋锁
 */
void spinlock_release(spinlock_t *lock) {
    if (!lock) {
        return;
    }
    
    __atomic_clear(&lock->locked, __ATOMIC_RELEASE);
}

/**
 * @brief 获取自旋锁并禁止中断
 */
void spinlock_acquire_irqsave(spinlock_t *lock) {
    if (!lock) {
        return;
    }
    
    lock->irq_saved = save_interrupts();
    spinlock_acquire(lock);
}

/**
 * @brief 释放自旋锁并恢复中断
 */
void spinlock_release_irqrestore(spinlock_t *lock) {
    if (!lock) {
        return;
    }
    
    spinlock_release(lock);
    restore_interrupts(lock->irq_saved);
}

/**
 * @brief 尝试获取自旋锁
 */
int spinlock_try_acquire(spinlock_t *lock) {
    if (!lock) {
        return 0;
    }
    
    return !__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE);
}
