/**
 * @file vmm.h
 * @brief 虚拟内存管理器 (Virtual Memory Manager)
 *
 * 实现虚拟地址空间管理，包括页表操作、地址映射、
 * 内存区域管理等功能。支持ARMv8-A 4级页表结构。
 */

#ifndef _VMM_H
#define _VMM_H

#include "mm.h"
#include "pmm.h"

/*============================================================================
 *                              配置常量
 *============================================================================*/

/* 虚拟地址空间布局 */
#define VMM_USER_BASE 0x0000000000000000UL  /* 用户空间起始 */
#define VMM_USER_END 0x0000FFFFFFFFFFFFUL   /* 用户空间结束 (256TB) */
#define VMM_KERNEL_BASE KERNEL_VIRT_BASE    /* 内核空间起始 */
#define VMM_KERNEL_END 0xFFFFFFFFFFFFFFFFUL /* 内核空间结束 */

/* 内核虚拟地址空间布局 */
#define VMM_KERNEL_CODE_START KERNEL_VIRT_BASE
#define VMM_KERNEL_CODE_END (KERNEL_VIRT_BASE + 0x00100000UL) /* 1MB 代码段 */
#define VMM_KERNEL_DATA_START VMM_KERNEL_CODE_END
#define VMM_KERNEL_DATA_END (KERNEL_VIRT_BASE + 0x00200000UL) /* 1MB 数据段 */
#define VMM_KERNEL_HEAP_START                                                  \
  (KERNEL_VIRT_BASE + 0x01000000UL) /* 堆起始: +16MB */
#define VMM_KERNEL_HEAP_END                                                    \
  (KERNEL_VIRT_BASE + 0x10000000UL) /* 堆结束: +256MB */
#define VMM_KERNEL_MMIO_START (KERNEL_VIRT_BASE + 0x80000000UL) /* MMIO 起始 \
                                                                 */
#define VMM_KERNEL_MMIO_END (KERNEL_VIRT_BASE + 0xC0000000UL)   /* MMIO 结束 */

/* 虚拟内存区域标志 */
#define VMA_FLAG_READ (1 << 0)   /* 可读 */
#define VMA_FLAG_WRITE (1 << 1)  /* 可写 */
#define VMA_FLAG_EXEC (1 << 2)   /* 可执行 */
#define VMA_FLAG_USER (1 << 3)   /* 用户可访问 */
#define VMA_FLAG_DEVICE (1 << 4) /* 设备内存 */
#define VMA_FLAG_SHARED (1 << 5) /* 共享内存 */
#define VMA_FLAG_STACK (1 << 6)  /* 栈区域 */
#define VMA_FLAG_HEAP (1 << 7)   /* 堆区域 */
#define VMA_FLAG_FIXED (1 << 8)  /* 固定地址 */

/* 默认权限组合 */
#define VMA_FLAG_KERNEL_RW (VMA_FLAG_READ | VMA_FLAG_WRITE)
#define VMA_FLAG_KERNEL_RX (VMA_FLAG_READ | VMA_FLAG_EXEC)
#define VMA_FLAG_KERNEL_RWX (VMA_FLAG_READ | VMA_FLAG_WRITE | VMA_FLAG_EXEC)
#define VMA_FLAG_USER_RW (VMA_FLAG_READ | VMA_FLAG_WRITE | VMA_FLAG_USER)
#define VMA_FLAG_USER_RX (VMA_FLAG_READ | VMA_FLAG_EXEC | VMA_FLAG_USER)
#define VMA_FLAG_USER_RWX                                                      \
  (VMA_FLAG_READ | VMA_FLAG_WRITE | VMA_FLAG_EXEC | VMA_FLAG_USER)

/*============================================================================
 *                              数据结构
 *============================================================================*/

/**
 * @brief 虚拟内存区域 (VMA)
 */
typedef struct vma {
  virt_addr_t vm_start;    /* 起始虚拟地址 */
  virt_addr_t vm_end;      /* 结束虚拟地址 (不包含) */
  phys_addr_t vm_phys;     /* 映射的物理地址 (0表示未映射或非线性映射) */
  uint64_t vm_flags;       /* 区域标志 */
  uint64_t vm_offset;      /* 文件偏移 (用于mmap) */
  struct vma *vm_next;     /* 下一个VMA */
  struct vma *vm_prev;     /* 上一个VMA */
  struct mm_struct *vm_mm; /* 所属地址空间 */
  const char *vm_name;     /* 区域名称 (调试用) */
} vma_t;

/**
 * @brief 内存描述符 (进程地址空间)
 */
typedef struct mm_struct {
  pgt_t pgd;               /* 页表基址 (L0表) */
  virt_addr_t start_code;  /* 代码段起始 */
  virt_addr_t end_code;    /* 代码段结束 */
  virt_addr_t start_data;  /* 数据段起始 */
  virt_addr_t end_data;    /* 数据段结束 */
  virt_addr_t start_brk;   /* 堆起始 */
  virt_addr_t brk;         /* 堆当前位置 */
  virt_addr_t start_stack; /* 栈起始 */
  vma_t *mmap;             /* VMA链表头 */
  uint64_t map_count;      /* VMA数量 */
  uint64_t total_vm;       /* 总虚拟页数 */
  uint64_t locked_vm;      /* 锁定页数 */
  uint32_t ref_count;      /* 引用计数 */
  volatile uint32_t lock;  /* 自旋锁 */
} mm_struct_t;

/**
 * @brief 页表遍历结果
 */
typedef struct pte_walker {
  pte_t *l0_entry; /* L0 表项指针 */
  pte_t *l1_entry; /* L1 表项指针 */
  pte_t *l2_entry; /* L2 表项指针 */
  pte_t *l3_entry; /* L3 表项指针 */
  pgt_t l1_table;  /* L1 表指针 */
  pgt_t l2_table;  /* L2 表指针 */
  pgt_t l3_table;  /* L3 表指针 */
} pte_walker_t;

/**
 * @brief 映射请求
 */
typedef struct map_request {
  virt_addr_t virt_start; /* 虚拟起始地址 */
  virt_addr_t virt_end;   /* 虚拟结束地址 */
  phys_addr_t phys_start; /* 物理起始地址 */
  uint64_t flags;         /* 映射标志 */
  uint64_t page_size;     /* 页面大小 (4KB/2MB/1GB) */
} map_request_t;

/*============================================================================
 *                              函数声明 - 初始化
 *============================================================================*/

/**
 * @brief 初始化虚拟内存管理器
 * @return 0 成功, 负数失败
 */
int vmm_init(void);

/**
 * @brief 初始化内核页表
 * @return 0 成功, 负数失败
 */
int vmm_init_kernel_pgt(void);

/*============================================================================
 *                              函数声明 - 页表操作
 *============================================================================*/

/**
 * @brief 创建新的页表
 * @return 页表指针, NULL 表示失败
 */
pgt_t vmm_create_pgt(void);

/**
 * @brief 销毁页表
 * @param pgt 页表指针
 */
void vmm_destroy_pgt(pgt_t pgt);

/**
 * @brief 切换当前页表
 * @param pgt 页表指针
 */
void vmm_switch_pgt(pgt_t pgt);

/**
 * @brief 获取当前页表
 * @return 当前页表指针
 */
pgt_t vmm_get_current_pgt(void);

/**
 * @brief 遍历页表获取PTE
 * @param va 虚拟地址
 * @param walker 输出遍历结果
 * @param alloc 是否分配缺失的中间页表
 * @return 最终PTE指针, NULL表示失败
 */
pte_t *vmm_walk_pt(virt_addr_t va, pte_walker_t *walker, bool alloc);

/**
 * @brief 获取虚拟地址的PTE
 * @param pgt 页表指针
 * @param va 虚拟地址
 * @param alloc 是否分配缺失的中间页表
 * @return PTE指针, NULL表示失败
 */
pte_t *vmm_get_pte(pgt_t pgt, virt_addr_t va, bool alloc);

/*============================================================================
 *                              函数声明 - 地址映射
 *============================================================================*/

/**
 * @brief 映射单个页面
 * @param pgt 页表指针
 * @param va 虚拟地址
 * @param pa 物理地址
 * @param flags 页表属性标志
 * @return 0 成功, 负数失败
 */
int vmm_map_page(pgt_t pgt, virt_addr_t va, phys_addr_t pa, uint64_t flags);

/**
 * @brief 映射页面范围
 * @param pgt 页表指针
 * @param va_start 起始虚拟地址
 * @param pa_start 起始物理地址
 * @param size 映射大小
 * @param flags 页表属性标志
 * @return 0 成功, 负数失败
 */
int vmm_map_range(pgt_t pgt, virt_addr_t va_start, phys_addr_t pa_start,
                  uint64_t size, uint64_t flags);

/**
 * @brief 映射大页 (2MB)
 * @param pgt 页表指针
 * @param va 虚拟地址 (需2MB对齐)
 * @param pa 物理地址 (需2MB对齐)
 * @param flags 页表属性标志
 * @return 0 成功, 负数失败
 */
int vmm_map_huge_page(pgt_t pgt, virt_addr_t va, phys_addr_t pa,
                      uint64_t flags);

/**
 * @brief 取消映射页面
 * @param pgt 页表指针
 * @param va 虚拟地址
 * @return 0 成功, 负数失败
 */
int vmm_unmap_page(pgt_t pgt, virt_addr_t va);

/**
 * @brief 取消映射页面范围
 * @param pgt 页表指针
 * @param va_start 起始虚拟地址
 * @param size 映射大小
 * @return 0 成功, 负数失败
 */
int vmm_unmap_range(pgt_t pgt, virt_addr_t va_start, uint64_t size);

/**
 * @brief 修改页面属性
 * @param pgt 页表指针
 * @param va 虚拟地址
 * @param flags 新的页表属性
 * @return 0 成功, 负数失败
 */
int vmm_protect_page(pgt_t pgt, virt_addr_t va, uint64_t flags);

/**
 * @brief 查询虚拟地址映射
 * @param pgt 页表指针
 * @param va 虚拟地址
 * @param pa_out 输出物理地址
 * @param flags_out 输出页表属性
 * @return 0 成功, 负数失败
 */
int vmm_query_mapping(pgt_t pgt, virt_addr_t va, phys_addr_t *pa_out,
                      uint64_t *flags_out);

/*============================================================================
 *                              函数声明 - VMA管理
 *============================================================================*/

/**
 * @brief 创建内存描述符
 * @return mm_struct指针, NULL表示失败
 */
mm_struct_t *vmm_create_mm(void);

/**
 * @brief 销毁内存描述符
 * @param mm 内存描述符指针
 */
void vmm_destroy_mm(mm_struct_t *mm);

/**
 * @brief 创建VMA
 * @param mm 内存描述符
 * @param start 起始虚拟地址
 * @param end 结束虚拟地址
 * @param flags VMA标志
 * @return VMA指针, NULL表示失败
 */
vma_t *vmm_create_vma(mm_struct_t *mm, virt_addr_t start, virt_addr_t end,
                      uint64_t flags);

/**
 * @brief 查找包含指定地址的VMA
 * @param mm 内存描述符
 * @param addr 虚拟地址
 * @return VMA指针, NULL表示未找到
 */
vma_t *vmm_find_vma(mm_struct_t *mm, virt_addr_t addr);

/**
 * @brief 删除VMA
 * @param mm 内存描述符
 * @param vma 要删除的VMA
 */
void vmm_remove_vma(mm_struct_t *mm, vma_t *vma);

/**
 * @brief 在VMA范围内映射物理内存
 * @param mm 内存描述符
 * @param vma VMA指针
 * @return 0 成功, 负数失败
 */
int vmm_map_vma(mm_struct_t *mm, vma_t *vma);

/*============================================================================
 *                              函数声明 - 内核映射
 *============================================================================*/

/**
 * @brief 映射内核空间
 * @param pgt 页表指针
 * @return 0 成功, 负数失败
 */
int vmm_map_kernel_space(pgt_t pgt);

/**
 * @brief 映射设备MMIO区域
 * @param pa 物理基地址
 * @param size 区域大小
 * @return 映射后的虚拟地址, 0表示失败
 */
virt_addr_t vmm_map_mmio(phys_addr_t pa, uint64_t size);

/**
 * @brief 取消映射设备MMIO区域
 * @param va 虚拟基地址
 * @param size 区域大小
 */
void vmm_unmap_mmio(virt_addr_t va, uint64_t size);

/**
 * @brief 分配内核虚拟地址空间
 * @param size 请求大小
 * @return 虚拟地址, 0表示失败
 */
virt_addr_t vmm_alloc_kernel_virt(uint64_t size);

/*============================================================================
 *                              函数声明 - TLB操作
 *============================================================================*/

/**
 * @brief 使TLB条目失效
 * @param va 虚拟地址
 */
void vmm_invalidate_tlb(virt_addr_t va);

/**
 * @brief 使整个TLB失效
 */
void vmm_invalidate_tlb_all(void);

/**
 * @brief 使指令TLB失效
 * @param va 虚拟地址
 */
void vmm_invalidate_itlb(virt_addr_t va);

/**
 * @brief 使数据TLB失效
 * @param va 虚拟地址
 */
void vmm_invalidate_dtlb(virt_addr_t va);

/*============================================================================
 *                              函数声明 - 调试
 *============================================================================*/

/**
 * @brief 打印页表信息
 * @param pgt 页表指针
 */
void vmm_print_pgt(pgt_t pgt);

/**
 * @brief 打印VMA信息
 * @param mm 内存描述符
 */
void vmm_print_vmas(mm_struct_t *mm);

/**
 * @brief 打印虚拟内存统计信息
 */
void vmm_print_stats(void);

/*============================================================================
 *                              内联辅助函数
 *============================================================================*/

/**
 * @brief 检查虚拟地址是否在用户空间
 */
static inline bool vmm_is_user_addr(virt_addr_t va) {
  return va <= VMM_USER_END;
}

/**
 * @brief 检查虚拟地址是否在内核空间
 */
static inline bool vmm_is_kernel_addr(virt_addr_t va) {
  return va >= VMM_KERNEL_BASE;
}

/**
 * @brief 检查地址范围是否有效
 */
static inline bool vmm_is_valid_range(virt_addr_t start, virt_addr_t end) {
  return start < end && (vmm_is_user_addr(start) == vmm_is_user_addr(end - 1));
}

/**
 * @brief VMA标志转页表属性
 */
static inline uint64_t vma_flags_to_pte(uint64_t vma_flags) {
  uint64_t pte_flags = PTE_VALID | PTE_PAGE | PTE_AF | PTE_SH_INNER_SHAREABLE |
                       PTE_ATTR_INDEX(MT_NORMAL);

  if (vma_flags & VMA_FLAG_WRITE) {
    pte_flags |= PTE_RW;
  } else {
    pte_flags |= PTE_RO;
  }

  if (!(vma_flags & VMA_FLAG_EXEC)) {
    pte_flags |= PTE_PXN | PTE_XN;
  }

  if (vma_flags & VMA_FLAG_USER) {
    pte_flags |= PTE_USER_RW | PTE_NG;
  }

  if (vma_flags & VMA_FLAG_DEVICE) {
    pte_flags &= ~PTE_ATTR_INDEX(MT_NORMAL);
    pte_flags |= PTE_ATTR_INDEX(MT_DEVICE_nGnRE);
    pte_flags |= PTE_PXN | PTE_XN;
  }

  return pte_flags;
}

#endif /* _VMM_H */
