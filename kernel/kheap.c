/**
 * @file kheap.c
 * @brief 内核堆管理器实现
 *
 * 实现内核动态内存分配，使用slab分配器管理小对象，
 * 伙伴系统管理大块内存。
 */

#include "kheap.h"
#include "io.h"
#include "pmm.h"
#include "vmm.h"
/*============================================================================
 *                              配置常量
 *============================================================================*/

/* Slab对象大小类别 */
static const uint64_t slab_sizes[SLAB_SIZE_CLASSES] = {
    8, 16, 32, 64, 96, 128, 192, 256, 512, 1024, 2048, 4096, 8192};

/*============================================================================
 *                              全局变量
 *============================================================================*/

/* Slab缓存数组 */
static slab_cache_t slab_caches[SLAB_SIZE_CLASSES];

/* 大块内存链表 */
static big_block_t *big_blocks = NULL;
static volatile uint32_t big_blocks_lock = 0;

/* 堆状态 */
static struct {
  bool initialized;
  virt_addr_t heap_start;
  virt_addr_t heap_end;
  virt_addr_t heap_current;
  uint64_t total_allocated;
  uint64_t total_freed;
  volatile uint32_t lock;
} kheap_state = {.initialized = false,
                 .heap_start = KHEAP_START,
                 .heap_end = KHEAP_END,
                 .heap_current = KHEAP_START,
                 .total_allocated = 0,
                 .total_freed = 0,
                 .lock = 0};

/*============================================================================
 *                              内联辅助函数
 *============================================================================*/

/**
 * @brief 获取自旋锁
 */
static inline void kheap_lock(volatile uint32_t *lock) {
  while (__atomic_test_and_set(lock, __ATOMIC_ACQUIRE)) {
    while (*lock) {
      __asm__ volatile("yield" ::: "memory");
    }
  }
}

/**
 * @brief 释放自旋锁
 */
static inline void kheap_unlock(volatile uint32_t *lock) {
  __atomic_clear(lock, __ATOMIC_RELEASE);
}

/**
 * @brief 内存填充 (用于调试)
 */
static inline void memfill(void *ptr, uint8_t val, uint64_t size) {
  uint8_t *p = (uint8_t *)ptr;
  while (size--) {
    *p++ = val;
  }
}

/*============================================================================
 *                              Slab分配器实现
 *============================================================================*/

/**
 * @brief 创建新的slab
 */
static slab_t *slab_create(slab_cache_t *cache) {
  /* 分配slab内存 (一个页面) */
  phys_addr_t pa = pmm_alloc_page(PMM_FLAG_ZERO);
  if (pa == 0) {
    return NULL;
  }

  virt_addr_t va = (virt_addr_t)PHYS_TO_VIRT(pa);

  /* slab描述符放在页面开头 */
  slab_t *slab = (slab_t *)va;

  /* 计算对象起始位置 (对齐) */
  uint64_t header_size = sizeof(slab_t);
  uint64_t obj_size = cache->object_size;

  /* 对齐到对象大小 */
  header_size = (header_size + obj_size - 1) & ~(obj_size - 1);

  void *obj_start = (void *)(va + header_size);
  uint64_t avail_size = PAGE_SIZE - header_size;
  uint64_t num_objects = avail_size / obj_size;

  /* 限制对象数量 */
  if (num_objects < SLAB_MIN_OBJECTS) {
    num_objects = SLAB_MIN_OBJECTS;
  }
  if (num_objects > SLAB_MAX_OBJECTS) {
    num_objects = SLAB_MAX_OBJECTS;
  }

  /* 初始化slab */
  slab->cache = cache;
  slab->inuse = 0;
  slab->total = num_objects;
  slab->flags = SLAB_FLAG_EMPTY;
  slab->next = NULL;
  slab->prev = NULL;

  /* 构建空闲链表 */
  slab->freelist = obj_start;
  void **prev = (void **)obj_start;

  for (uint64_t i = 0; i < num_objects - 1; i++) {
    void *next = (void *)((uint8_t *)obj_start + (i + 1) * obj_size);
    *prev = next;
    prev = (void **)next;
  }
  *prev = NULL; /* 最后一个 */

  return slab;
}

/**
 * @brief 销毁slab
 */
static void slab_destroy(slab_t *slab) {
  if (!slab) {
    return;
  }

  phys_addr_t pa = VIRT_TO_PHYS(slab);
  pmm_free_page(pa);
}

/**
 * @brief 从slab分配对象
 */
static void *slab_alloc_obj(slab_t *slab) {
  if (!slab || !slab->freelist) {
    return NULL;
  }

  void *obj = slab->freelist;
  slab->freelist = *(void **)obj;
  slab->inuse++;

  /* 更新状态 */
  if (slab->inuse == slab->total) {
    slab->flags = SLAB_FLAG_FULL;
  } else {
    slab->flags = SLAB_FLAG_PARTIAL;
  }

  return obj;
}

/**
 * @brief 释放对象到slab
 */
static void slab_free_obj(slab_t *slab, void *obj) {
  if (!slab || !obj) {
    return;
  }

  /* 添加到空闲链表 */
  *(void **)obj = slab->freelist;
  slab->freelist = obj;
  slab->inuse--;

  /* 更新状态 */
  if (slab->inuse == 0) {
    slab->flags = SLAB_FLAG_EMPTY;
  } else {
    slab->flags = SLAB_FLAG_PARTIAL;
  }
}

/**
 * @brief 初始化slab缓存
 */
static void slab_cache_init(slab_cache_t *cache, const char *name,
                            uint64_t obj_size) {
  cache->name = name;
  cache->object_size = obj_size;
  cache->align = obj_size < 8 ? 8 : obj_size;
  cache->flags = 0;
  cache->slabs_partial = NULL;
  cache->slabs_full = NULL;
  cache->slabs_free = NULL;
  cache->num_slabs = 0;
  cache->num_active = 0;
  cache->num_allocs = 0;
  cache->num_frees = 0;
  cache->lock = 0;
}

/**
 * @brief 从缓存分配对象
 */
static void *slab_cache_alloc(slab_cache_t *cache, uint32_t flags) {
  void *obj = NULL;

  kheap_lock(&cache->lock);

  /* 优先从partial链表分配 */
  if (cache->slabs_partial) {
    obj = slab_alloc_obj(cache->slabs_partial);

    /* 如果slab满了，移到full链表 */
    if (cache->slabs_partial->flags == SLAB_FLAG_FULL) {
      slab_t *slab = cache->slabs_partial;

      /* 从partial链表移除 */
      cache->slabs_partial = slab->next;
      if (slab->next) {
        slab->next->prev = NULL;
      }

      /* 添加到full链表 */
      slab->next = cache->slabs_full;
      if (cache->slabs_full) {
        cache->slabs_full->prev = slab;
      }
      cache->slabs_full = slab;
      slab->prev = NULL;
    }
  }
  /* 从free链表分配 */
  else if (cache->slabs_free) {
    obj = slab_alloc_obj(cache->slabs_free);

    slab_t *slab = cache->slabs_free;

    /* 从free链表移除 */
    cache->slabs_free = slab->next;
    if (slab->next) {
      slab->next->prev = NULL;
    }

    /* 添加到partial链表 */
    slab->next = cache->slabs_partial;
    if (cache->slabs_partial) {
      cache->slabs_partial->prev = slab;
    }
    cache->slabs_partial = slab;
    slab->prev = NULL;
  }
  /* 创建新slab */
  else {
    slab_t *slab = slab_create(cache);
    if (slab) {
      cache->num_slabs++;
      obj = slab_alloc_obj(slab);

      /* 添加到partial链表 */
      slab->next = cache->slabs_partial;
      if (cache->slabs_partial) {
        cache->slabs_partial->prev = slab;
      }
      cache->slabs_partial = slab;
    }
  }

  if (obj) {
    cache->num_active++;
    cache->num_allocs++;
  }

  kheap_unlock(&cache->lock);

  /* 清零 */
  if (obj && (flags & KMALLOC_ZERO)) {
    memzero(obj, cache->object_size);
  }

  return obj;
}

/**
 * @brief 释放对象到缓存
 */
static void slab_cache_free(slab_cache_t *cache, void *obj) {
  if (!obj) {
    return;
  }

  kheap_lock(&cache->lock);

  /* 查找对象所属的slab */
  /* 简化实现: 假设对象地址所在的页面就是slab */
  virt_addr_t va = (virt_addr_t)obj;
  virt_addr_t page_start = va & PAGE_MASK;
  slab_t *slab = (slab_t *)page_start;

  /* 验证slab */
  if (slab->cache != cache) {
    kheap_unlock(&cache->lock);
    return;
  }

  uint8_t old_flags = slab->flags;
  slab_free_obj(slab, obj);

  /* 更新链表 */
  if (old_flags == SLAB_FLAG_FULL && slab->flags == SLAB_FLAG_PARTIAL) {
    /* 从full移到partial */
    if (slab->prev) {
      slab->prev->next = slab->next;
    } else {
      cache->slabs_full = slab->next;
    }
    if (slab->next) {
      slab->next->prev = slab->prev;
    }

    slab->next = cache->slabs_partial;
    if (cache->slabs_partial) {
      cache->slabs_partial->prev = slab;
    }
    cache->slabs_partial = slab;
    slab->prev = NULL;
  } else if (old_flags == SLAB_FLAG_PARTIAL && slab->flags == SLAB_FLAG_EMPTY) {
    /* 从partial移到free */
    if (slab->prev) {
      slab->prev->next = slab->next;
    } else {
      cache->slabs_partial = slab->next;
    }
    if (slab->next) {
      slab->next->prev = slab->prev;
    }

    slab->next = cache->slabs_free;
    if (cache->slabs_free) {
      cache->slabs_free->prev = slab;
    }
    cache->slabs_free = slab;
    slab->prev = NULL;
  }

  cache->num_active--;
  cache->num_frees++;

  kheap_unlock(&cache->lock);
}

/*============================================================================
 *                              大块内存分配
 *============================================================================*/

/**
 * @brief 分配大块内存 (超过8KB)
 */
static void *big_block_alloc(uint64_t size, uint32_t flags) {
  /* 计算需要的页数 */
  uint64_t total_size = size + sizeof(big_block_t);
  total_size = PAGE_ALIGN(total_size);
  uint64_t pages = total_size / PAGE_SIZE;

  /* 分配物理内存 */
  phys_addr_t pa = pmm_alloc_pages(pages, PMM_FLAG_ZERO);
  if (pa == 0) {
    return NULL;
  }

  /* 映射到内核虚拟地址空间 */
  virt_addr_t va = vmm_alloc_kernel_virt(total_size);
  if (va == 0) {
    pmm_free_pages(pa, pages);
    return NULL;
  }

  /* 映射 */
  uint64_t pte_flags = PTE_KERNEL_DATA;
  if (vmm_map_range(vmm_get_current_pgt(), va, pa, total_size, pte_flags) < 0) {
    pmm_free_pages(pa, pages);
    return NULL;
  }

  /* 设置块头 */
  big_block_t *block = (big_block_t *)va;
  block->magic = BIG_BLOCK_MAGIC;
  block->size = size;
  block->flags = flags;

  /* 添加到链表 */
  kheap_lock(&big_blocks_lock);
  block->next = big_blocks;
  big_blocks = block;
  kheap_unlock(&big_blocks_lock);

  kheap_state.total_allocated += total_size;

  /* 返回用户指针 */
  return (void *)(va + sizeof(big_block_t));
}

/**
 * @brief 释放大块内存
 */
static void big_block_free(void *ptr) {
  if (!ptr) {
    return;
  }

  virt_addr_t va = (virt_addr_t)ptr - sizeof(big_block_t);
  big_block_t *block = (big_block_t *)va;

  /* 验证魔数 */
  if (block->magic != BIG_BLOCK_MAGIC) {
    return;
  }

  /* 从链表移除 */
  kheap_lock(&big_blocks_lock);

  big_block_t **pp = &big_blocks;
  while (*pp) {
    if (*pp == block) {
      *pp = block->next;
      break;
    }
    pp = &(*pp)->next;
  }

  kheap_unlock(&big_blocks_lock);

  /* 计算大小 */
  uint64_t total_size = block->size + sizeof(big_block_t);
  total_size = PAGE_ALIGN(total_size);
  uint64_t pages = total_size / PAGE_SIZE;

  /* 获取物理地址 */
  phys_addr_t pa;
  if (vmm_query_mapping(vmm_get_current_pgt(), va, &pa, NULL) == 0) {
    /* 取消映射 */
    vmm_unmap_range(vmm_get_current_pgt(), va, total_size);

    /* 释放物理内存 */
    pmm_free_pages(pa, pages);
  }

  kheap_state.total_freed += total_size;
}

/**
 * @brief 查找大块内存块
 */
static big_block_t *find_big_block(void *ptr) {
  if (!ptr) {
    return NULL;
  }

  virt_addr_t va = (virt_addr_t)ptr - sizeof(big_block_t);

  kheap_lock(&big_blocks_lock);

  big_block_t *block = big_blocks;
  while (block) {
    if ((virt_addr_t)block == va) {
      kheap_unlock(&big_blocks_lock);
      return block;
    }
    block = block->next;
  }

  kheap_unlock(&big_blocks_lock);

  return NULL;
}

/*============================================================================
 *                              公共接口实现
 *============================================================================*/

/**
 * @brief 初始化内核堆
 */
int kheap_init(void) {
  if (kheap_state.initialized) {
    return 0;
  }

  /* 初始化slab缓存 */
  for (int i = 0; i < SLAB_SIZE_CLASSES; i++) {
    char name[32];
    /* 简化: 使用静态名称 */
    static const char *cache_names[SLAB_SIZE_CLASSES] = {
        "kmalloc-8",   "kmalloc-16",  "kmalloc-32",  "kmalloc-64",
        "kmalloc-96",  "kmalloc-128", "kmalloc-192", "kmalloc-256",
        "kmalloc-512", "kmalloc-1k",  "kmalloc-2k",  "kmalloc-4k",
        "kmalloc-8k"};

    slab_cache_init(&slab_caches[i], cache_names[i], slab_sizes[i]);
  }

  kheap_state.initialized = true;

  return 0;
}

/**
 * @brief 内核内存分配
 */
void *kmalloc(uint64_t size, uint32_t flags) {
  if (size == 0) {
    return NULL;
  }

  /* 对齐大小 */
  size = kheap_align_size(size);

  /* 检查是否使用slab */
  int idx = slab_size_index(size);

  if (idx >= 0) {
    /* 使用slab分配 */
    return slab_cache_alloc(&slab_caches[idx], flags);
  }

  /* 使用大块分配 */
  return big_block_alloc(size, flags);
}

/**
 * @brief 内核内存释放
 */
void kfree(void *ptr) {
  if (!ptr) {
    return;
  }

  /* 检查是否是大块 */
  if (find_big_block(ptr)) {
    big_block_free(ptr);
    return;
  }

  /* 查找对应的slab缓存 */
  virt_addr_t va = (virt_addr_t)ptr;
  virt_addr_t page_start = va & PAGE_MASK;
  slab_t *slab = (slab_t *)page_start;

  if (slab && slab->cache) {
    slab_cache_free(slab->cache, ptr);
  }
}

/**
 * @brief 内核内存重分配
 */
void *krealloc(void *ptr, uint64_t new_size, uint32_t flags) {
  if (new_size == 0) {
    kfree(ptr);
    return NULL;
  }

  if (!ptr) {
    return kmalloc(new_size, flags);
  }

  /* 获取原大小 */
  uint64_t old_size = ksize(ptr);

  if (new_size <= old_size) {
    return ptr;
  }

  /* 分配新内存 */
  void *new_ptr = kmalloc(new_size, flags);
  if (!new_ptr) {
    return NULL;
  }

  /* 复制数据 */
  memcpy(new_ptr, ptr, old_size);

  /* 释放旧内存 */
  kfree(ptr);

  return new_ptr;
}

/**
 * @brief 分配对齐内存
 */
void *kmalloc_aligned(uint64_t size, uint64_t align, uint32_t flags) {
  if (size == 0 || align == 0) {
    return NULL;
  }

  /* 对齐必须是2的幂 */
  if ((align & (align - 1)) != 0) {
    return NULL;
  }

  /* 如果对齐要求小于等于slab对象大小，直接用kmalloc */
  if (align <= PAGE_SIZE) {
    return kmalloc(size, flags);
  }

  /* 大对齐: 分配额外空间用于对齐 */
  uint64_t total = size + align;
  void *ptr = kmalloc(total, flags);
  if (!ptr) {
    return NULL;
  }

  /* 对齐 */
  virt_addr_t addr = (virt_addr_t)ptr;
  virt_addr_t aligned = (addr + align - 1) & ~(align - 1);

  return (void *)aligned;
}

/**
 * @brief 分配DMA内存
 */
void *kmalloc_dma(uint64_t size, uint32_t flags) {
  /* DMA内存需要物理连续且在低地址 */
  flags |= KMALLOC_DMA;

  /* 对于DMA，使用大块分配确保物理连续 */
  return big_block_alloc(size, flags);
}

/**
 * @brief 获取内存块大小
 */
uint64_t ksize(void *ptr) {
  if (!ptr) {
    return 0;
  }

  /* 检查是否是大块 */
  big_block_t *block = find_big_block(ptr);
  if (block) {
    return block->size;
  }

  /* 查找slab */
  virt_addr_t va = (virt_addr_t)ptr;
  virt_addr_t page_start = va & PAGE_MASK;
  slab_t *slab = (slab_t *)page_start;

  if (slab && slab->cache) {
    return slab->cache->object_size;
  }

  return 0;
}

/**
 * @brief 创建slab缓存
 */
kmem_cache_t *kmem_cache_create(const char *name, uint64_t size, uint64_t align,
                                uint32_t flags) {
  slab_cache_t *cache = kzalloc(sizeof(slab_cache_t), GFP_KERNEL);
  if (!cache) {
    return NULL;
  }

  cache->name = name;
  cache->object_size = size;
  cache->align = align > 8 ? align : 8;
  cache->flags = flags;
  cache->lock = 0;

  return (kmem_cache_t *)cache;
}

/**
 * @brief 销毁slab缓存
 */
void kmem_cache_destroy(kmem_cache_t *cache) {
  if (!cache) {
    return;
  }

  slab_cache_t *sc = (slab_cache_t *)cache;

  /* 释放所有slab */
  slab_t *slab;

  while ((slab = sc->slabs_free)) {
    sc->slabs_free = slab->next;
    slab_destroy(slab);
  }

  while ((slab = sc->slabs_partial)) {
    sc->slabs_partial = slab->next;
    slab_destroy(slab);
  }

  kfree(cache);
}

/**
 * @brief 从缓存分配对象
 */
void *kmem_cache_alloc(kmem_cache_t *cache, uint32_t flags) {
  if (!cache) {
    return NULL;
  }

  return slab_cache_alloc((slab_cache_t *)cache, flags);
}

/**
 * @brief 释放对象到缓存
 */
void kmem_cache_free(kmem_cache_t *cache, void *obj) {
  if (!cache || !obj) {
    return;
  }

  slab_cache_free((slab_cache_t *)cache, obj);
}

/*============================================================================
 *                              调试函数
 *============================================================================*/

/**
 * @brief 检查堆完整性
 */
int kheap_check(void) {
  /* 检查大块链表 */
  kheap_lock(&big_blocks_lock);

  big_block_t *block = big_blocks;
  while (block) {
    if (block->magic != BIG_BLOCK_MAGIC) {
      kheap_unlock(&big_blocks_lock);
      return -1;
    }
    block = block->next;
  }

  kheap_unlock(&big_blocks_lock);

  return 0;
}

/**
 * @brief 打印堆统计信息
 */
void kheap_print_stats(void) {
  /* 实际应使用printk */
  /*
  printk("KHeap: Kernel Heap Statistics\n");
  printk("  Heap Range: 0x%lx - 0x%lx\n",
         kheap_state.heap_start, kheap_state.heap_end);
  printk("  Total Allocated: %lu bytes\n", kheap_state.total_allocated);
  printk("  Total Freed: %lu bytes\n", kheap_state.total_freed);
  */
}

/**
 * @brief 打印所有slab缓存信息
 */
void kheap_print_caches(void) {
  /* 实际应使用printk */
  for (int i = 0; i < SLAB_SIZE_CLASSES; i++) {
    slab_cache_t *cache = &slab_caches[i];
    /*
    printk("Cache %s: size=%lu, slabs=%lu, active=%lu, allocs=%lu,
    frees=%lu\n", cache->name, cache->object_size, cache->num_slabs,
           cache->num_active, cache->num_allocs, cache->num_frees);
    */
    (void)cache;
  }
}
