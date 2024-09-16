/**
 * @file kheap.h
 * @brief 内核堆管理器 (Kernel Heap Manager)
 *
 * 实现内核动态内存分配，使用slab分配器管理小对象，
 * 伙伴系统管理大块内存。提供kmalloc/kfree接口。
 */

#ifndef _KHEAP_H
#define _KHEAP_H

#include "mm.h"
#include "vmm.h"

/*============================================================================
 *                              配置常量
 *============================================================================*/

/* 堆起始地址 */
#define KHEAP_START VMM_KERNEL_HEAP_START
#define KHEAP_END VMM_KERNEL_HEAP_END
#define KHEAP_SIZE (KHEAP_END - KHEAP_START) /* 240MB */

/* 最小分配单位 */
#define KHEAP_MIN_ALIGN 8 /* 最小对齐 */
#define KHEAP_MIN_SIZE 16 /* 最小分配大小 */

/* Slab分配器配置 */
#define SLAB_MIN_SIZE 8      /* 最小slab对象大小 */
#define SLAB_MAX_SIZE 8192   /* 最大slab对象大小 (8KB) */
#define SLAB_SIZE_CLASSES 13 /* size class数量 */

/* 大块分配阈值 (超过此值使用伙伴系统) */
#define KMALLOC_MAX_CACHE SLAB_MAX_SIZE

/* 分配标志 */
#define KMALLOC_NORMAL 0x0000  /* 普通分配 */
#define KMALLOC_ATOMIC 0x0001  /* 原子分配 (不可睡眠) */
#define KMALLOC_ZERO 0x0002    /* 分配并清零 */
#define KMALLOC_DMA 0x0004     /* DMA可用内存 */
#define KMALLOC_NOWAIT 0x0008  /* 不等待 */
#define KMALLOC_HIGHMEM 0x0010 /* 高端内存 (暂不支持) */

/* 常用组合 */
#define GFP_KERNEL KMALLOC_NORMAL
#define GFP_ATOMIC (KMALLOC_ATOMIC | KMALLOC_NOWAIT)
#define GFP_ZERO KMALLOC_ZERO
#define GFP_DMA KMALLOC_DMA

/*============================================================================
 *                              配置常量
 *============================================================================*/

/* 每个slab中的对象数量 */
#define SLAB_MIN_OBJECTS 8
#define SLAB_MAX_OBJECTS 256

/* Slab标志 */
#define SLAB_FLAG_EMPTY 0   /* 空 */
#define SLAB_FLAG_PARTIAL 1 /* 部分使用 */
#define SLAB_FLAG_FULL 2    /* 满 */

/*============================================================================
 *                              数据结构
 *============================================================================*/

/**
 * @brief 大块内存块头
 */
typedef struct big_block {
  uint64_t magic;         /* 魔数 */
  uint64_t size;          /* 块大小 */
  uint64_t flags;         /* 分配标志 */
  struct big_block *next; /* 链表下一个 */
} big_block_t;

#define BIG_BLOCK_MAGIC 0xDEADBEEFCAFEBABEULL
/*============================================================================
 *                              数据结构
 *============================================================================*/

/**
 * @brief Slab描述符
 */
typedef struct slab {
  void *freelist;           /* 空闲对象链表 */
  uint16_t inuse;           /* 已使用对象数 */
  uint16_t total;           /* 总对象数 */
  uint16_t size;            /* 对象大小 */
  uint16_t flags;           /* 标志 */
  phys_addr_t phys_addr;    /* 物理地址 */
  struct slab *next;        /* 下一个slab */
  struct slab *prev;        /* 上一个slab */
  struct kmem_cache *cache; /* 所属cache */
} slab_t;

/**
 * @brief Slab缓存
 */
typedef struct slab_cache {
  const char *name;     /* 缓存名称 */
  uint64_t object_size; /* 对象大小 */
  uint64_t align;       /* 对齐要求 */
  uint32_t flags;       /* 缓存标志 */

  slab_t *slabs_partial; /* 部分使用的slab链表 */
  slab_t *slabs_full;    /* 完全使用的slab链表 */
  slab_t *slabs_free;    /* 空闲slab链表 */

  uint64_t num_slabs;  /* slab总数 */
  uint64_t num_active; /* 活跃对象数 */
  uint64_t num_allocs; /* 分配次数 */
  uint64_t num_frees;  /* 释放次数 */

  volatile uint32_t lock; /* 自旋锁 */
} slab_cache_t;

/**
 * @brief Slab缓存 (kmem_cache)
 */
typedef struct kmem_cache {
  const char *name;          /* 缓存名称 */
  uint32_t object_size;      /* 对象大小 */
  uint32_t objects_per_slab; /* 每个slab的对象数 */
  uint32_t flags;            /* 缓存标志 */

  /* Slab链表 */
  slab_t *slabs_full;    /* 已满slab链表 */
  slab_t *slabs_partial; /* 部分使用slab链表 */
  slab_t *slabs_empty;   /* 空闲slab链表 */

  /* 统计信息 */
  uint32_t nr_slabs;  /* slab总数 */
  uint32_t nr_active; /* 活跃对象数 */
  uint32_t nr_allocs; /* 分配次数 */
  uint32_t nr_frees;  /* 释放次数 */

  /* 锁 */
  volatile uint32_t lock;

  struct kmem_cache *next; /* 下一个cache */
} kmem_cache_t;

/* Cache标志 */
#define CACHE_FLAG_NONE 0x0000
#define CACHE_FLAG_DMA 0x0001  /* DMA可用 */
#define CACHE_FLAG_ZERO 0x0002 /* 分配时清零 */

/**
 * @brief 内核堆管理器
 */
typedef struct kheap_manager {
  /* 堆范围 */
  virt_addr_t heap_start;   /* 堆起始地址 */
  virt_addr_t heap_end;     /* 堆结束地址 */
  virt_addr_t heap_current; /* 当前堆顶 */

  /* Slab缓存 */
  kmem_cache_t *caches;                        /* 缓存链表头 */
  kmem_cache_t size_caches[SLAB_SIZE_CLASSES]; /* 预定义size缓存 */

  /* 统计信息 */
  uint64_t total_allocated; /* 总分配量 */
  uint64_t total_freed;     /* 总释放量 */
  uint64_t current_usage;   /* 当前使用量 */
  uint64_t peak_usage;      /* 峰值使用量 */

  /* 锁 */
  volatile uint32_t lock;
} kheap_manager_t;

/**
 * @brief 内存块头部 (用于大块分配)
 */
typedef struct mem_block {
  uint64_t magic;         /* 魔数校验 */
  uint64_t size;          /* 块大小 (不含头部) */
  uint32_t flags;         /* 分配标志 */
  uint32_t reserved;      /* 保留 */
  struct mem_block *next; /* 下一个块 */
  struct mem_block *prev; /* 上一个块 */
} mem_block_t;

/* 魔数 */
#define MEM_BLOCK_MAGIC 0x4D454D424C4B4850ULL      /* "MEMBLKHP" */
#define MEM_BLOCK_MAGIC_FREE 0x46524545424C4B48ULL /* "FREEBLKH" */

/*============================================================================
 *                              函数声明 - 初始化
 *============================================================================*/

/**
 * @brief 初始化内核堆
 * @return 0 成功, 负数失败
 */
int kheap_init(void);

/**
 * @brief 扩展内核堆
 * @param size 扩展大小
 * @return 0 成功, 负数失败
 */
int kheap_expand(uint64_t size);

/*============================================================================
 *                              函数声明 - kmalloc接口
 *============================================================================*/

/**
 * @brief 内核内存分配
 * @param size 请求大小
 * @param flags 分配标志
 * @return 内存指针, NULL表示失败
 */
void *kmalloc(uint64_t size, uint32_t flags);

/**
 * @brief 内核内存分配 (对齐)
 * @param size 请求大小
 * @param align 对齐要求 (必须是2的幂)
 * @param flags 分配标志
 * @return 内存指针, NULL表示失败
 */
void *kmalloc_align(uint64_t size, uint64_t align, uint32_t flags);

/**
 * @brief 内核内存释放
 * @param ptr 内存指针
 */
void kfree(void *ptr);

/**
 * @brief 重分配内存
 * @param ptr 原内存指针
 * @param new_size 新大小
 * @param flags 分配标志
 * @return 新内存指针, NULL表示失败
 */
void *krealloc(void *ptr, uint64_t new_size, uint32_t flags);

/**
 * @brief 分配并清零
 * @param size 请求大小
 * @param flags 分配标志
 * @return 内存指针, NULL表示失败
 */
void *kzalloc(uint64_t size, uint32_t flags);

/**
 * @brief 分配数组并清零
 * @param n 元素数量
 * @param size 元素大小
 * @param flags 分配标志
 * @return 内存指针, NULL表示失败
 */
void *kcalloc(uint64_t n, uint64_t size, uint32_t flags);

/*============================================================================
 *                              函数声明 - Slab缓存
 *============================================================================*/

/**
 * @brief 创建slab缓存
 * @param name 缓存名称
 * @param object_size 对象大小
 * @param flags 缓存标志
 * @return 缓存指针, NULL表示失败
 */
kmem_cache_t *kmem_cache_create(const char *name, uint64_t size, uint64_t align,
                                uint32_t flags);

/**
 * @brief 销毁slab缓存
 * @param cache 缓存指针
 */
void kmem_cache_destroy(kmem_cache_t *cache);

/**
 * @brief 从缓存分配对象
 * @param cache 缓存指针
 * @return 对象指针, NULL表示失败
 */
void *kmem_cache_alloc(kmem_cache_t *cache, uint32_t flags);

/**
 * @brief 释放对象到缓存
 * @param cache 缓存指针
 * @param obj 对象指针
 */
void kmem_cache_free(kmem_cache_t *cache, void *obj);

/**
 * @brief 获取缓存统计信息
 * @param cache 缓存指针
 * @param nr_slabs 输出slab数
 * @param nr_active 输出活跃对象数
 */
void kmem_cache_stats(kmem_cache_t *cache, uint32_t *nr_slabs,
                      uint32_t *nr_active);

/*============================================================================
 *                              函数声明 - 调试
 *============================================================================*/

/**
 * @brief 检查堆完整性
 * @return 0 正常, 负数错误
 */
int kheap_check(void);

/**
 * @brief 打印堆统计信息
 */
void kheap_print_stats(void);

/**
 * @brief 打印所有slab缓存信息
 */
void kheap_print_caches(void);

/**
 * @brief 获取内存块大小
 * @param ptr 内存指针
 * @return 块大小, 0表示无效
 */
uint64_t ksize(void *ptr);

/*============================================================================
 *                              内联辅助函数
 *============================================================================*/

/**
 * @brief 获取size对应的slab size class索引
 */
static inline int slab_size_index(uint64_t size) {
  if (size <= 8)
    return 0;
  if (size <= 16)
    return 1;
  if (size <= 32)
    return 2;
  if (size <= 64)
    return 3;
  if (size <= 96)
    return 4;
  if (size <= 128)
    return 5;
  if (size <= 192)
    return 6;
  if (size <= 256)
    return 7;
  if (size <= 512)
    return 8;
  if (size <= 1024)
    return 9;
  if (size <= 2048)
    return 10;
  if (size <= 4096)
    return 11;
  if (size <= 8192)
    return 12;
  return -1; /* 超出slab范围 */
}

/**
 * @brief 获取对齐后的分配大小
 */
static inline uint64_t kheap_align_size(uint64_t size) {
  if (size < KHEAP_MIN_SIZE) {
    size = KHEAP_MIN_SIZE;
  }
  /* 向上对齐到8字节 */
  return (size + KHEAP_MIN_ALIGN - 1) & ~(KHEAP_MIN_ALIGN - 1);
}

/**
 * @brief 检查指针是否在堆范围内
 */
static inline bool kheap_is_heap_addr(void *ptr) {
  virt_addr_t addr = (virt_addr_t)ptr;
  return addr >= KHEAP_START && addr < KHEAP_END;
}

#endif /* _KHEAP_H */
