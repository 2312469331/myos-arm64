#include "slab.h"
#include <libc.h>
#include "types.h"
#include "mmu.h"
#include "pmm.h"

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

  // if (arm64_map_range(va, pa, span)) {
  //   free_phys_pages(pa, order);
  //   free_slab_page_meta(sp);
  //   return NULL;
  // }

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


  if (!sp)
    return;

  slab_cache_del_page(sp->cache, sp);
  // arm64_unmap_range(sp->va, span);
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
  // size_t span;

  order = get_order_ul(size);
  // span = PAGE_SIZE << order;

  la = alloc_large_meta();
  if (!la)
    return NULL;

  pa = alloc_phys_pages(order);
  if (!pa) {
    free_large_meta(la);
    return NULL;
  }

  va = (uintptr_t)phys_to_virt(pa);
  // slab不需要搞页表:
  //  if (arm64_map_range(va, pa, span)) {
  //    free_phys_pages(pa, order);
  //    free_large_meta(la);
  //    return NULL;
  //  }

  la->va = va;
  la->pa = pa;
  la->size = size;
  la->order = order;

  return (void *)va;
}

static void large_free(struct large_alloc *la) {
  // size_t span;

  if (!la)
    return;

  // span = PAGE_SIZE << la->order;
  free_phys_pages(la->pa, la->order);
  // arm64_unmap_range(la->va, span);
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
