#include "slab.h"
#include <libc.h>

/* =========================================================
 * 基本常量
 * =========================================================
 */

#define PAGE_SHIFT 12UL
#define PAGE_SIZE (1UL << PAGE_SHIFT)
#define PAGE_MASK (~(PAGE_SIZE - 1))

#define SZ_8K (8UL * 1024)
#define SZ_4M (4UL * 1024 * 1024)

#define SLAB_CACHE_COUNT 11

/*
 * 纯基础实现，元数据池固定大小。
 * 如果你觉得不够，可以调大。
 */
#define MAX_SLAB_PAGES 4096
#define MAX_LARGE_ALLOCS 2048

#define SLAB_MAGIC 0x534C4142UL  /* "SLAB" */
#define LARGE_MAGIC 0x4C415247UL /* "LARG" */

/* =========================================================
 * ARM64 4KB granule, 4-level page table
 * =========================================================
 *
 * VA[47:39] -> L0
 * VA[38:30] -> L1
 * VA[29:21] -> L2
 * VA[20:12] -> L3
 * VA[11:0]  -> page offset
 */

#define PTRS_PER_PTE 512UL
#define ARM64_PTE_VALID (1UL << 0)
#define ARM64_PTE_TYPE_BLOCK (0UL << 1)
#define ARM64_PTE_TYPE_TABLE (1UL << 1)
#define ARM64_PTE_TYPE_PAGE (1UL << 1)

/*
 * 下面这些属性位需要你按自己内核的 MAIR/TCR 配置确认。
 * 这里给出一版最常见的 normal memory inner shareable kernel RW 模板。
 */
#define ARM64_PTE_AF (1UL << 10)
#define ARM64_PTE_SH_INNER (3UL << 8)
#define ARM64_PTE_AP_RW_KERNEL (0UL << 6)
#define ARM64_PTE_ATTRIDX(idx) ((unsigned long)(idx) << 2)
#define ARM64_PTE_UXN (1UL << 54)
#define ARM64_PTE_PXN (1UL << 53)

/*
 * 默认按 AttrIndx=0 映射普通内存。
 * 若你的 MAIR[0] 不是 normal memory，请修改。
 */
#define ARM64_PAGE_PROT                                                        \
  (ARM64_PTE_VALID | ARM64_PTE_TYPE_PAGE | ARM64_PTE_AF | ARM64_PTE_SH_INNER | \
   ARM64_PTE_AP_RW_KERNEL | ARM64_PTE_ATTRIDX(0) | ARM64_PTE_UXN |             \
   ARM64_PTE_PXN)

#define ARM64_TABLE_PROT (ARM64_PTE_VALID | ARM64_PTE_TYPE_TABLE)

#define ARM64_PTE_ADDR_MASK 0x0000FFFFFFFFF000UL

#define L0_INDEX(va) (((unsigned long)(va) >> 39) & 0x1FFUL)
#define L1_INDEX(va) (((unsigned long)(va) >> 30) & 0x1FFUL)
#define L2_INDEX(va) (((unsigned long)(va) >> 21) & 0x1FFUL)
#define L3_INDEX(va) (((unsigned long)(va) >> 12) & 0x1FFUL)

typedef uint64_t pte_t;

/* =========================================================
 * 线性映射区
 * =========================================================
 */

uintptr_t slab_linear_map_base;
phys_addr_t slab_l0_table_pa;

static inline void *phys_to_virt(phys_addr_t pa) {
  return (void *)(slab_linear_map_base + (uintptr_t)pa);
}

static inline phys_addr_t virt_to_phys(const void *va) {
  return (phys_addr_t)((uintptr_t)va - slab_linear_map_base);
}

static inline bool va_in_linear_map(const void *va) {
  uintptr_t v = (uintptr_t)va;

  return v >= slab_linear_map_base;
}

/* =========================================================
 * 基础工具
 * =========================================================
 */

static inline unsigned long align_up_ul(unsigned long x, unsigned long a) {
  return (x + a - 1) & ~(a - 1);
}

static inline unsigned int ilog2_ul(unsigned long x) {
  unsigned int r = 0;

  while (x > 1) {
    x >>= 1;
    r++;
  }
  return r;
}

static inline unsigned int get_order_ul(unsigned long size) {
  unsigned long pages;
  unsigned long n = 1;
  unsigned int order = 0;

  if (!size)
    return 0;

  pages = (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
  while (n < pages) {
    n <<= 1;
    order++;
  }
  return order;
}

static inline bool pte_present(pte_t pte) { return !!(pte & ARM64_PTE_VALID); }

static inline phys_addr_t pte_to_phys(pte_t pte) {
  return (phys_addr_t)(pte & ARM64_PTE_ADDR_MASK);
}

static bool pte_table_empty(pte_t *table) {
  unsigned int i;

  for (i = 0; i < PTRS_PER_PTE; i++) {
    if (pte_present(table[i]))
      return false;
  }
  return true;
}

/* =========================================================
 * 页表映射/拆映射
 * =========================================================
 */

static int arm64_map_one_page(uintptr_t va, phys_addr_t pa) {
  pte_t *l0, *l1, *l2, *l3;
  phys_addr_t new_pa;
  unsigned long idx0, idx1, idx2, idx3;

  if (!slab_l0_table_pa)
    return -1;

  l0 = (pte_t *)phys_to_virt(slab_l0_table_pa);

  idx0 = L0_INDEX(va);
  idx1 = L1_INDEX(va);
  idx2 = L2_INDEX(va);
  idx3 = L3_INDEX(va);

  if (!pte_present(l0[idx0])) {
    new_pa = alloc_phys_pages(0);
    if (!new_pa)
      return -1;
    memset(phys_to_virt(new_pa), 0, PAGE_SIZE);
    l0[idx0] = (pte_t)new_pa | ARM64_TABLE_PROT;
  }
  l1 = (pte_t *)phys_to_virt(pte_to_phys(l0[idx0]));

  if (!pte_present(l1[idx1])) {
    new_pa = alloc_phys_pages(0);
    if (!new_pa)
      return -1;
    memset(phys_to_virt(new_pa), 0, PAGE_SIZE);
    l1[idx1] = (pte_t)new_pa | ARM64_TABLE_PROT;
  }
  l2 = (pte_t *)phys_to_virt(pte_to_phys(l1[idx1]));

  if (!pte_present(l2[idx2])) {
    new_pa = alloc_phys_pages(0);
    if (!new_pa)
      return -1;
    memset(phys_to_virt(new_pa), 0, PAGE_SIZE);
    l2[idx2] = (pte_t)new_pa | ARM64_TABLE_PROT;
  }
  l3 = (pte_t *)phys_to_virt(pte_to_phys(l2[idx2]));

  l3[idx3] = (pte_t)pa | ARM64_PAGE_PROT;

  /*
   * 真机上通常还需要:
   *   dsb ishst;
   *   tlbi ... (必要时)
   *   dsb ish;
   *   isb;
   *
   * 但你要求只做最基础分配逻辑，这里不加同步/屏障细节。
   */
  return 0;
}

static void arm64_unmap_one_page(uintptr_t va) {
  pte_t *l0, *l1, *l2, *l3;
  phys_addr_t l1_pa, l2_pa, l3_pa;
  unsigned long idx0, idx1, idx2, idx3;

  if (!slab_l0_table_pa)
    return;

  l0 = (pte_t *)phys_to_virt(slab_l0_table_pa);

  idx0 = L0_INDEX(va);
  idx1 = L1_INDEX(va);
  idx2 = L2_INDEX(va);
  idx3 = L3_INDEX(va);

  if (!pte_present(l0[idx0]))
    return;

  l1_pa = pte_to_phys(l0[idx0]);
  l1 = (pte_t *)phys_to_virt(l1_pa);

  if (!pte_present(l1[idx1]))
    return;

  l2_pa = pte_to_phys(l1[idx1]);
  l2 = (pte_t *)phys_to_virt(l2_pa);

  if (!pte_present(l2[idx2]))
    return;

  l3_pa = pte_to_phys(l2[idx2]);
  l3 = (pte_t *)phys_to_virt(l3_pa);

  if (!pte_present(l3[idx3]))
    return;

  /* 1. 先让 L3 页项失效 */
  l3[idx3] = 0;

  /* 2. 检查整个 L3 表是否空，空则释放，并让 L2 对应项失效 */
  if (pte_table_empty(l3)) {
    l2[idx2] = 0;
    free_phys_pages(l3_pa, 0);

    /* 3. 检查整个 L2 表是否空，空则释放，并让 L1 对应项失效 */
    if (pte_table_empty(l2)) {
      l1[idx1] = 0;
      free_phys_pages(l2_pa, 0);

      /* 4. 检查整个 L1 表是否空，空则释放，并让 L0 对应项失效 */
      if (pte_table_empty(l1)) {
        l0[idx0] = 0;
        free_phys_pages(l1_pa, 0);
      }
    }
  }
}

static int arm64_map_range(uintptr_t va, phys_addr_t pa, size_t size) {
  size_t off, len;

  len = align_up_ul(size, PAGE_SIZE);
  for (off = 0; off < len; off += PAGE_SIZE) {
    if (arm64_map_one_page(va + off, pa + off))
      return -1;
  }
  return 0;
}

static void arm64_unmap_range(uintptr_t va, size_t size) {
  size_t off, len;

  len = align_up_ul(size, PAGE_SIZE);
  for (off = 0; off < len; off += PAGE_SIZE)
    arm64_unmap_one_page(va + off);
}

/* =========================================================
 * slab 元数据
 * =========================================================
 */

struct slab_cache;

struct slab_page {
  unsigned long magic;

  struct slab_cache *cache;
  struct slab_page *next;

  phys_addr_t pa;
  uintptr_t va;

  unsigned int order;
  size_t obj_size;
  unsigned int capacity;
  unsigned int inuse;

  void *freelist;
  bool used;
};

struct slab_cache {
  size_t size;
  struct slab_page *partial;
};

struct large_alloc {
  unsigned long magic;

  uintptr_t va;
  phys_addr_t pa;
  size_t size;
  unsigned int order;
  bool used;
};

static const size_t slab_sizes[SLAB_CACHE_COUNT] = {
    8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};

static struct slab_cache slab_caches[SLAB_CACHE_COUNT];
static struct slab_page slab_pages[MAX_SLAB_PAGES];
static struct large_alloc large_allocs[MAX_LARGE_ALLOCS];

/* =========================================================
 * slab_page / large_alloc 池管理
 * =========================================================
 */

static struct slab_page *alloc_slab_page_meta(void) {
  unsigned int i;

  for (i = 0; i < MAX_SLAB_PAGES; i++) {
    if (!slab_pages[i].used) {
      memset(&slab_pages[i], 0, sizeof(slab_pages[i]));
      slab_pages[i].used = true;
      slab_pages[i].magic = SLAB_MAGIC;
      return &slab_pages[i];
    }
  }
  return NULL;
}

static void free_slab_page_meta(struct slab_page *sp) {
  if (!sp)
    return;

  memset(sp, 0, sizeof(*sp));
}

static struct large_alloc *alloc_large_meta(void) {
  unsigned int i;

  for (i = 0; i < MAX_LARGE_ALLOCS; i++) {
    if (!large_allocs[i].used) {
      memset(&large_allocs[i], 0, sizeof(large_allocs[i]));
      large_allocs[i].used = true;
      large_allocs[i].magic = LARGE_MAGIC;
      return &large_allocs[i];
    }
  }
  return NULL;
}

static void free_large_meta(struct large_alloc *la) {
  if (!la)
    return;

  memset(la, 0, sizeof(*la));
}

/* =========================================================
 * slab cache 辅助
 * =========================================================
 */

static struct slab_cache *find_slab_cache(size_t size) {
  unsigned int i;

  for (i = 0; i < SLAB_CACHE_COUNT; i++) {
    if (size <= slab_caches[i].size)
      return &slab_caches[i];
  }
  return NULL;
}

static unsigned int slab_order_for_size(size_t obj_size) {
  /*
   * 一个 slab 至少能容纳一个对象即可。
   * 外部元数据管理，不占 slab 页内空间。
   */
  return get_order_ul(obj_size);
}

static void slab_cache_add_page(struct slab_cache *cache,
                                struct slab_page *sp) {
  sp->next = cache->partial;
  cache->partial = sp;
}

static void slab_cache_del_page(struct slab_cache *cache,
                                struct slab_page *sp) {
  struct slab_page **pp = &cache->partial;

  while (*pp) {
    if (*pp == sp) {
      *pp = sp->next;
      sp->next = NULL;
      return;
    }
    pp = &(*pp)->next;
  }
}

static struct slab_page *slab_find_page_by_va(void *va) {
  unsigned int i;
  uintptr_t addr = (uintptr_t)va;

  for (i = 0; i < MAX_SLAB_PAGES; i++) {
    uintptr_t start, end;
    size_t span;

    if (!slab_pages[i].used || slab_pages[i].magic != SLAB_MAGIC)
      continue;

    start = slab_pages[i].va;
    span = PAGE_SIZE << slab_pages[i].order;
    end = start + span;

    if (addr >= start && addr < end)
      return &slab_pages[i];
  }
  return NULL;
}

static struct large_alloc *large_find_by_va(void *va) {
  unsigned int i;
  uintptr_t addr = (uintptr_t)va;

  for (i = 0; i < MAX_LARGE_ALLOCS; i++) {
    if (!large_allocs[i].used || large_allocs[i].magic != LARGE_MAGIC)
      continue;
    if (large_allocs[i].va == addr)
      return &large_allocs[i];
  }
  return NULL;
}

/* =========================================================
 * slab 页面创建 / 销毁
 * =========================================================
 */

static struct slab_page *slab_grow(struct slab_cache *cache) {
  struct slab_page *sp;
  phys_addr_t pa;
  uintptr_t va;
  unsigned int order;
  size_t span;
  unsigned int capacity, i;
  char *base;

  order = slab_order_for_size(cache->size);
  span = PAGE_SIZE << order;
  capacity = span / cache->size;
  if (!capacity)
    return NULL;

  sp = alloc_slab_page_meta();
  if (!sp)
    return NULL;

  pa = alloc_phys_pages(order);
  if (!pa) {
    free_slab_page_meta(sp);
    return NULL;
  }

  va = (uintptr_t)phys_to_virt(pa);

  if (arm64_map_range(va, pa, span)) {
    free_phys_pages(pa, order);
    free_slab_page_meta(sp);
    return NULL;
  }

  sp->cache = cache;
  sp->pa = pa;
  sp->va = va;
  sp->order = order;
  sp->obj_size = cache->size;
  sp->capacity = capacity;
  sp->inuse = 0;
  sp->freelist = NULL;

  base = (char *)va;
  for (i = 0; i < capacity; i++) {
    void *obj = base + i * cache->size;

    *(void **)obj = sp->freelist;
    sp->freelist = obj;
  }

  slab_cache_add_page(cache, sp);
  return sp;
}

static void slab_destroy_page(struct slab_page *sp) {
  size_t span;

  if (!sp)
    return;

  span = PAGE_SIZE << sp->order;

  slab_cache_del_page(sp->cache, sp);
  arm64_unmap_range(sp->va, span);
  free_phys_pages(sp->pa, sp->order);
  free_slab_page_meta(sp);
}

/* =========================================================
 * slab alloc / free
 * =========================================================
 */

static void *slab_alloc(struct slab_cache *cache) {
  struct slab_page *sp;
  void *obj;

  for (sp = cache->partial; sp; sp = sp->next) {
    if (sp->freelist)
      break;
  }

  if (!sp) {
    sp = slab_grow(cache);
    if (!sp)
      return NULL;
  }

  obj = sp->freelist;
  sp->freelist = *(void **)obj;
  sp->inuse++;

  return obj;
}

static void slab_free_obj(void *va) {
  struct slab_page *sp;

  sp = slab_find_page_by_va(va);
  if (!sp)
    return;

  *(void **)va = sp->freelist;
  sp->freelist = va;

  if (sp->inuse)
    sp->inuse--;

  if (sp->inuse == 0)
    slab_destroy_page(sp);
}

/* =========================================================
 * direct buddy alloc / free
 * =========================================================
 */

static void *large_alloc(size_t size) {
  struct large_alloc *la;
  phys_addr_t pa;
  uintptr_t va;
  unsigned int order;
  size_t span;

  order = get_order_ul(size);
  span = PAGE_SIZE << order;

  la = alloc_large_meta();
  if (!la)
    return NULL;

  pa = alloc_phys_pages(order);
  if (!pa) {
    free_large_meta(la);
    return NULL;
  }

  va = (uintptr_t)phys_to_virt(pa);

  if (arm64_map_range(va, pa, span)) {
    free_phys_pages(pa, order);
    free_large_meta(la);
    return NULL;
  }

  la->va = va;
  la->pa = pa;
  la->size = size;
  la->order = order;

  return (void *)va;
}

static void large_free(struct large_alloc *la) {
  size_t span;

  if (!la)
    return;

  span = PAGE_SIZE << la->order;

  /*
   * 用户要求：
   *   释放时先把页让伙伴系统 free，
   *   把 L3 项失效，然后逐级检查空表并回收。
   *
   * 真实工程里通常更推荐先 unmap 再 free，
   * 但这里按你的要求顺序写。
   */
  free_phys_pages(la->pa, la->order);
  arm64_unmap_range(la->va, span);
  free_large_meta(la);
}

/* =========================================================
 * 对外接口
 * =========================================================
 */

void slab_init(void) {
  unsigned int i;

  memset(slab_caches, 0, sizeof(slab_caches));
  memset(slab_pages, 0, sizeof(slab_pages));
  memset(large_allocs, 0, sizeof(large_allocs));

  for (i = 0; i < SLAB_CACHE_COUNT; i++)
    slab_caches[i].size = slab_sizes[i];
}

void *kmalloc(size_t size) {
  struct slab_cache *cache;

  if (!size)
    return NULL;

  if (size > SZ_4M)
    return NULL;

  if (size <= SZ_8K) {
    cache = find_slab_cache(size);
    if (!cache)
      return NULL;
    return slab_alloc(cache);
  }

  return large_alloc(size);
}

void kfree(void *va) {
  struct large_alloc *la;

  if (!va)
    return;

  if (!va_in_linear_map(va))
    return;

  /*
   * 先判断 direct buddy alloc
   * 这里要求传入的是 kmalloc 返回的原始地址。
   */
  la = large_find_by_va(va);
  if (la) {
    large_free(la);
    return;
  }

  /*
   * 否则按 slab 对象处理
   */
  slab_free_obj(va);
}
