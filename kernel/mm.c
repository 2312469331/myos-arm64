/**
 * @file mm.c
 * @brief 内存管理初始化和总体控制
 * 
 * 实现内存管理子系统的初始化流程，协调PMM、VMM和KHeap
 * 的工作。提供内存统计和调试接口。
 */

#include "mm.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "io.h"

/*============================================================================
 *                              全局变量
 *============================================================================*/

/* 内存管理器状态 */
static struct {
    bool        initialized;        /* 是否已初始化 */
    mem_stats_t stats;              /* 内存统计信息 */
} mm_state = {
    .initialized = false,
    .stats = {0}
};

/* 外部页表声明 (来自启动代码) */
extern uint64_t ttbr0_l0[512];
extern uint64_t ttbr1_l0[512];
extern uint64_t l1_table[512];
extern uint64_t l2_table[512];
extern uint64_t l3_table[512];

/*============================================================================
 *                              内部函数
 *============================================================================*/

/**
 * @brief 配置MAIR_EL1寄存器
 * 设置内存属性索引
 */
static void setup_mair_el1(void) {
    uint64_t mair = MAIR_EL1_INIT;
    __asm__ volatile ("msr MAIR_EL1, %0" :: "r" (mair));
}

/**
 * @brief 配置TCR_EL1寄存器
 * 设置页表属性和地址范围
 */
static void setup_tcr_el1(void) {
    uint64_t tcr;
    
    /* 读取当前TCR_EL1 */
    __asm__ volatile ("mrs %0, TCR_EL1" : "=r" (tcr));
    
    /* 
     * TCR_EL1 配置:
     * - IPS[34:32]: 中间物理地址大小 (根据实际物理内存配置)
     * - TG1[31:30]: TTBR1粒度 (0=4KB)
     * - SH1[29:28]: TTBR1共享属性 (3=Inner Shareable)
     * - ORGN1[27:26]: TTBR1外部缓存属性 (1=Write-Back)
     * - IRGN1[25:24]: TTBR1内部缓存属性 (1=Write-Back)
     * - EPD1[23]: TTBR1使能 (0=使能)
     * - A1[22]: ASID选择 (0=TTBR0)
     * - T1SZ[21:16]: TTBR1大小 (25=39位虚拟地址)
     * - TG0[15:14]: TTBR0粒度 (0=4KB)
     * - SH0[13:12]: TTBR0共享属性 (3=Inner Shareable)
     * - ORGN0[11:10]: TTBR0外部缓存属性 (1=Write-Back)
     * - IRGN0[9:8]: TTBR0内部缓存属性 (1=Write-Back)
     * - EPD0[7]: TTBR0使能 (0=使能)
     * - T0SZ[5:0]: TTBR0大小 (25=39位虚拟地址)
     */
    
    /* 配置为4KB页面，Inner Shareable，Write-Back缓存 */
    tcr = (0ULL << 32) |     /* IPS: 32位物理地址 */
          (0ULL << 30) |     /* TG1: 4KB */
          (3ULL << 28) |     /* SH1: Inner Shareable */
          (1ULL << 26) |     /* ORGN1: Write-Back */
          (1ULL << 24) |     /* IRGN1: Write-Back */
          (0ULL << 23) |     /* EPD1: Enable TTBR1 */
          (0ULL << 22) |     /* A1: TTBR0 defines ASID */
          (25ULL << 16) |    /* T1SZ: 39-bit VA for TTBR1 */
          (0ULL << 14) |     /* TG0: 4KB */
          (3ULL << 12) |     /* SH0: Inner Shareable */
          (1ULL << 10) |     /* ORGN0: Write-Back */
          (1ULL << 8) |      /* IRGN0: Write-Back */
          (0ULL << 7) |      /* EPD0: Enable TTBR0 */
          (25ULL << 0);      /* T0SZ: 39-bit VA for TTBR0 */
    
    __asm__ volatile ("msr TCR_EL1, %0" :: "r" (tcr));
}

/**
 * @brief 配置SCTLR_EL1寄存器
 * 启用MMU和缓存
 */
static void setup_sctlr_el1(void) {
    uint64_t sctlr;
    
    __asm__ volatile ("mrs %0, SCTLR_EL1" : "=r" (sctlr));
    
    /*
     * SCTLR_EL1 配置:
     * - M[0]: MMU使能
     * - A[1]: 对齐检查
     * - C[2]: 数据缓存使能
     * - I[12]: 指令缓存使能
     */
    sctlr |= (1 << 0);   /* M: Enable MMU */
    sctlr |= (1 << 2);   /* C: Enable data cache */
    sctlr |= (1 << 12);  /* I: Enable instruction cache */
    
    /* 确保所有指令序列化 */
    dsb();
    isb();
    
    __asm__ volatile ("msr SCTLR_EL1, %0" :: "r" (sctlr));
    isb();
}

/**
 * @brief 初始化系统寄存器
 */
static void init_system_registers(void) {
    /* 配置内存属性 */
    setup_mair_el1();
    
    /* 配置页表控制 */
    setup_tcr_el1();
    
    /* 确保配置生效 */
    dsb();
    isb();
}

/*============================================================================
 *                              公共函数实现
 *============================================================================*/

/**
 * @brief 内存管理初始化入口
 * 
 * 初始化顺序:
 * 1. 系统寄存器配置 (MAIR, TCR)
 * 2. 物理内存管理器 (PMM)
 * 3. 虚拟内存管理器 (VMM)
 * 4. 内核堆管理器 (KHeap)
 * 
 * @return 0 成功, 负数失败
 */
int mm_init(void) {
    int ret;
    
    /* 防止重复初始化 */
    if (mm_state.initialized) {
        return 0;
    }
    
    /* 1. 初始化系统寄存器 */
    init_system_registers();
    
    /* 2. 初始化物理内存管理器
     * 假设物理内存从PHYS_BASE开始，大小为256MB
     * 实际应该从设备树或固件获取内存信息
     */
    ret = pmm_init(PHYS_BASE, 256 * 1024 * 1024);
    if (ret < 0) {
        return ret;
    }
    
    /* 保留内核镜像占用的内存 */
    ret = pmm_reserve_region(PHYS_BASE, KERNEL_IMAGE_SIZE);
    if (ret < 0) {
        return ret;
    }
    
    /* 保留页表占用的内存 */
    /* 页表位于内核镜像之后 */
    phys_addr_t pgt_start = PHYS_BASE + KERNEL_IMAGE_SIZE;
    ret = pmm_reserve_region(pgt_start, 5 * PAGE_SIZE); /* 5个页表 */
    if (ret < 0) {
        return ret;
    }
    
    /* 3. 初始化虚拟内存管理器 */
    ret = vmm_init();
    if (ret < 0) {
        return ret;
    }
    
    /* 4. 初始化内核堆 */
    ret = kheap_init();
    if (ret < 0) {
        return ret;
    }
    
    mm_state.initialized = true;
    
    return 0;
}

/**
 * @brief 获取内存统计信息
 * @param stats 统计信息结构体指针
 */
void mm_get_stats(mem_stats_t *stats) {
    if (!stats) {
        return;
    }
    
    /* 从PMM获取统计信息 */
    pmm_get_stats(stats);
}

/**
 * @brief 打印内存布局信息
 */
void mm_print_layout(void) {
    /* 使用简单的打印方式 (实际应使用printk) */
    /* 这里仅展示布局结构 */
    
    /*
     * 内存布局:
     * 
     * 物理内存:
     * 0x40000000 - 0x401FFFFF : 内核镜像 (2MB)
     * 0x40200000 - 0x40204FFF : 页表 (5 * 4KB)
     * 0x40205000 - ...        : 可用物理内存
     * 
     * 虚拟内存:
     * 0xFFFF000000000000 - 0xFFFF0000001FFFFF : 内核代码/数据 (2MB)
     * 0xFFFF000001000000 - 0xFFFF00000FFFFFFF : 内核堆 (240MB)
     * 0xFFFF000080000000 - 0xFFFF0000BFFFFFFF : MMIO区域 (1GB)
     */
}

/**
 * @brief 内存完整性检查
 * @return 0 正常, 负数错误
 */
int mm_check_integrity(void) {
    if (!mm_state.initialized) {
        return -1;
    }
    
    /* 检查PMM状态 */
    if (pmm_get_free_pages() > pmm_get_total_pages()) {
        return -2;
    }
    
    /* 检查KHeap状态 */
    if (kheap_check() < 0) {
        return -3;
    }
    
    return 0;
}

/**
 * @brief 获取可用内存大小
 * @return 可用内存字节数
 */
uint64_t mm_get_available_memory(void) {
    return pmm_get_free_pages() * PAGE_SIZE;
}

/**
 * @brief 获取总内存大小
 * @return 总内存字节数
 */
uint64_t mm_get_total_memory(void) {
    return pmm_get_total_pages() * PAGE_SIZE;
}

/*============================================================================
 *                              调试函数
 *============================================================================*/

#ifdef MM_DEBUG

/**
 * @brief 打印页表项详情
 * @param pte 页表项
 */
void mm_print_pte(pte_t pte) {
    if (!(pte & PTE_VALID)) {
        /* 无效页表项 */
        return;
    }
    
    /* 解析页表项属性 */
    uint64_t addr = pte & PTE_ADDR_MASK;
    uint64_t ap = (pte >> 6) & 0x3;
    uint64_t sh = (pte >> 8) & 0x3;
    uint64_t attr = (pte >> 2) & 0x7;
    
    /* 打印详细信息 */
    (void)addr;
    (void)ap;
    (void)sh;
    (void)attr;
}

/**
 * @brief 遍历并打印页表
 * @param pgt 页表指针
 * @param level 当前级别
 * @param va 当前虚拟地址
 */
void mm_walk_page_table(pgt_t pgt, int level, virt_addr_t va) {
    if (!pgt || level > PT_L3) {
        return;
    }
    
    for (int i = 0; i < PT_ENTRIES; i++) {
        pte_t pte = pgt[i];
        
        if (!(pte & PTE_VALID)) {
            continue;
        }
        
        virt_addr_t cur_va = va | ((uint64_t)i << (12 + (3 - level) * 9));
        
        if (level == PT_L3) {
            /* L3: 页表项 */
            mm_print_pte(pte);
        } else if (pte & PTE_TABLE) {
            /* 表描述符，递归遍历 */
            pgt_t next_pgt = (pgt_t)(pte & PTE_ADDR_MASK);
            mm_walk_page_table(next_pgt, level + 1, cur_va);
        } else if (pte & PTE_BLOCK) {
            /* 块描述符 */
            mm_print_pte(pte);
        }
    }
}

#endif /* MM_DEBUG */
