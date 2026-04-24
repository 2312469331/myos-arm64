/*
 * buddy.c - ARM64 bare-metal buddy physical memory allocator
 *
 * 特点：
 *  - 4KB 页大小
 *  - 管理物理地址范围默认：
 *  - order: 0 ~ 10（1页 ~ 1024页）
 *  - 纯 C，无 libc、无 OS 依赖
 *  - 适用于裸机内核物理页管理：页表、DMA、内核堆底层页分配
 *
 * 说明：
 *  - alloc_phys_pages(order, GFP_KERNEL) 返回连续物理页块起始物理地址
 *  - free_phys_pages(pa, order) 释放对应块，并自动尝试与伙伴合并
 *  - 失败时 alloc 返回 0
 *
 * 作者说明：666
 */
#include <pmm.h>
#include <mm_defs.h>

/* 链表操作函数和 container_of 宏已在 list.h 中定义 */

/* ============================================================
 * 1. 页面元数据
 * ============================================================
 */

/* 所有页的元数据数组（在 pmm.h 中声明为 extern） */
struct page mem_map[TOTAL_PAGES];

struct free_area {
  struct list_head free_list;
  u32 nr_free; /* 当前 order 的空闲块数量 */
};

/* 多级空闲链表 */
static struct free_area free_area[NR_ORDERS];

static volatile enum buddy_error buddy_last_error = BUDDY_OK;

enum buddy_error buddy_get_last_error(void) { return buddy_last_error; }

static void buddy_set_error(enum buddy_error err) { buddy_last_error = err; }

/* ============================================================
 * 2. 地址与页框号转换（内部使用）
 * ============================================================
 */

static int pa_in_range(phys_addr_t pa) {
  return (pa >= BUDDY_MEM_START) && (pa <= BUDDY_MEM_END);
}

static int valid_order(unsigned int order) { return (order <= MAX_ORDER); }

static int page_aligned(phys_addr_t pa) {
  return ((pa & (PAGE_SIZE - 1UL)) == 0);
}

/* ============================================================
 * 3. buddy 核心辅助逻辑
 * ============================================================
 */

/*
 * 伙伴页框号计算：
 * 对于 order = k，块大小 = 2^k 页
 * buddy_pfn = pfn ^ (1 << k)
 */
static unsigned long buddy_pfn(unsigned long pfn, unsigned int order) {
  return pfn ^ (1UL << order);
}

/*
 * 判断给定 pfn 是否能作为某个 order 块的首页
 */
static int pfn_aligned_to_order(unsigned long pfn, unsigned int order) {
  unsigned long pages = (1UL << order);
  return ((pfn & (pages - 1UL)) == 0);
}

/*
 * 向 free_area[order] 插入一个空闲块
 */
static void add_to_free_area(unsigned long pfn, unsigned int order) {
  struct page *page = pfn_to_page(pfn);

  mark_page_buddy(page, order);
  list_add_tail(&page->node, &free_area[order].free_list);
  free_area[order].nr_free++;
}

/*
 * 从 free_area[order] 删除一个空闲块
 */
static void remove_from_free_area(unsigned long pfn, unsigned int order) {
  struct page *page = pfn_to_page(pfn);

  list_del(&page->node);
  free_area[order].nr_free--;

  /* 从空闲链表摘下后，不再是 buddy 空闲块 */
  clear_page_flags(page);
}

/*
 * 将一个较大的块不断拆分，直到得到目标 order
 *
 * 输入：
 *  - pfn: 当前大块首页
 *  - high: 当前大块阶
 *  - low: 目标阶
 *
 * 拆分方式：
 *  每次把块一分为二：
 *   - 左半继续拆/最终返回
 *   - 右半放入更低一级 free_area
 */
static unsigned long expand_block(unsigned long pfn, unsigned int high,
                                  unsigned int low) {
  while (high > low) {
    unsigned int new_order = high - 1;
    unsigned long right_pfn = pfn + (1UL << new_order);

    add_to_free_area(right_pfn, new_order);
    high = new_order;
  }

  return pfn;
}

/*
 * 找到一个 >= order 的空闲块并分裂
 * 成功返回首页 pfn，失败返回 TOTAL_PAGES 作为非法值
 */
static unsigned long buddy_alloc_pfn(unsigned int order) {
  unsigned int cur;

  for (cur = order; cur <= MAX_ORDER; cur++) {
    if (!list_empty(&free_area[cur].free_list)) {
      struct list_head *node = free_area[cur].free_list.next;
      struct page *page = container_of(node, struct page, node);
      unsigned long pfn = (unsigned long)(page - &mem_map[0]);

      remove_from_free_area(pfn, cur);

      pfn = expand_block(pfn, cur, order);

      return pfn;
    }
  }

  return TOTAL_PAGES;
}

/*
 * 尝试将释放的块与其伙伴不断合并
 */
static unsigned long buddy_merge_pfn(unsigned long pfn, unsigned int *order) {
  while (*order < MAX_ORDER) {
    unsigned long bpfn = buddy_pfn(pfn, *order);
    struct page *buddy;

    if (bpfn >= TOTAL_PAGES)
      break;

    buddy = pfn_to_page(bpfn);

    /*
     * 只有当 buddy：
     *  1. 当前处于空闲链表中
     *  2. 阶数相同
     * 才能合并
     */
    if (!page_is_buddy(buddy))
      break;

    if (buddy->order != *order)
      break;

    /*
     * 伙伴块一定是该 order 的首页。
     * 将 buddy 从 free_area 中移除，然后向上合并。
     */
    remove_from_free_area(bpfn, *order);

    if (bpfn < pfn)
      pfn = bpfn;

    (*order)++;
  }

  return pfn;
}

/* ============================================================
 * 4. 初始化
 * ============================================================
 */

/*
 * 将整个管理区域按"尽可能大的且地址对齐的块"加入 buddy 系统
 *
 * 这样既保证：
 *  - 全部内存都被纳入管理
 *  - 初始碎片最少
 *
 * 举例：
 * 如果从 0 页开始，通常会优先放入大量 order=10 块
 * 剩余尾部再用更小 order 补齐。
 */
void buddy_init(void) {
  unsigned long i;
  unsigned long pfn = 0;
  unsigned long remain = TOTAL_PAGES;

  buddy_set_error(BUDDY_OK);

  /* 初始化 free_area */
  for (i = 0; i < NR_ORDERS; i++) {
    INIT_LIST_HEAD(&free_area[i].free_list);
    free_area[i].nr_free = 0;
  }

  /* 初始化所有 page 元数据 */
  for (i = 0; i < TOTAL_PAGES; i++) {
    mem_map[i].order = 0;
    mem_map[i].private = 0;
    mem_map[i].flags = 0;
    INIT_LIST_HEAD(&mem_map[i].node);
  }

  /*
   * 如果有内核镜像、自举页表、设备保留区等，
   * 可以先全部标记 reserved，再单独释放可用区。
   *
   * 当前示例：默认整个区间都可分配。
   */

  /* 先全部标记为 reserved，再按块释放到 buddy */
  for (i = 0; i < TOTAL_PAGES; i++) {
    mark_page_reserved(&mem_map[i]);
  }

  while (remain > 0) {
    unsigned int order = MAX_ORDER;
    unsigned long block_pages;

    /*
     * 找到一个：
     *  1. 不超过剩余页数
     *  2. pfn 对齐
     * 的最大 order
     */
    while (1) {
      block_pages = (1UL << order);

      if (block_pages <= remain && pfn_aligned_to_order(pfn, order))
        break;

      if (order == 0)
        break;

      order--;
    }

    clear_page_flags(&mem_map[pfn]);
    add_to_free_area(pfn, order);

    /* 非首页页仅保留"已被系统管理但不是块头"的普通状态 */
    for (i = 1; i < (1UL << order); i++) {
      clear_page_flags(&mem_map[pfn + i]);
    }

    pfn += (1UL << order);
    remain -= (1UL << order);
  }
}

/* ============================================================
 * 5. 对外分配接口
 * ============================================================
 */

/*
 * alloc_phys_pages - 分配 2^order 个连续物理页
 *
 * 返回：
 *  - 成功：起始物理地址
 *  - 失败：0
 */
phys_addr_t alloc_phys_pages(unsigned int order, gfp_t flags) {
  unsigned long pfn;
  struct page *page;


  if (!valid_order(order)) {
    buddy_set_error(BUDDY_ERR_BAD_ORDER);
    return (phys_addr_t)0;
  }

  pfn = buddy_alloc_pfn(order);
  if (pfn >= TOTAL_PAGES) {
    buddy_set_error(BUDDY_ERR_NO_MEMORY);
    return (phys_addr_t)0;
  }

  page = pfn_to_page(pfn);
  mark_page_allocated(page, order);

  /*
   * 对于块中其他页，这里不强制写特殊状态。
   * 实际内核中可以根据需要记录更多元信息。
   */

  buddy_set_error(BUDDY_OK);
  return pfn_to_pa(pfn);
}

/* ============================================================
 * 6. 对外释放接口
 * ============================================================
 */

/*
 * free_phys_pages - 释放 2^order 个连续物理页
 *
 * 注意：
 *  - pa 必须是当初 alloc 返回的块起始地址
 *  - order 必须与分配时一致
 */
void free_phys_pages(phys_addr_t pa, unsigned int order) {
  unsigned long pfn;
  struct page *page;

  if (!valid_order(order)) {
    buddy_set_error(BUDDY_ERR_BAD_ORDER);
    return;
  }

  if (!pa_in_range(pa)) {
    buddy_set_error(BUDDY_ERR_OUT_OF_RANGE);
    return;
  }

  if (!page_aligned(pa)) {
    buddy_set_error(BUDDY_ERR_UNALIGNED);
    return;
  }

  pfn = pa_to_pfn(pa);

  if (!pfn_aligned_to_order(pfn, order)) {
    /*
     * 释放地址不是该 order 块的合法首页
     */
    buddy_set_error(BUDDY_ERR_UNALIGNED);
    return;
  }

  if (pfn >= TOTAL_PAGES) {
    buddy_set_error(BUDDY_ERR_OUT_OF_RANGE);
    return;
  }

  page = pfn_to_page(pfn);

  /*
   * 基本一致性检查：
   * - 必须是已分配块首页
   * - order 应匹配
   */
  if (!page_is_allocated(page) || !(page->flags & PG_HEAD)) {
    if (page_is_buddy(page))
      buddy_set_error(BUDDY_ERR_DOUBLE_FREE);
    else
      buddy_set_error(BUDDY_ERR_NOT_ALLOCATED);
    return;
  }

  if (page->order != order) {
    buddy_set_error(BUDDY_ERR_BAD_ORDER);
    return;
  }

  /* 清掉 allocated 标记，准备并入 buddy */
  clear_page_flags(page);

  pfn = buddy_merge_pfn(pfn, &order);
  add_to_free_area(pfn, order);

  buddy_set_error(BUDDY_OK);
}

/* ============================================================
 * 7. 调试/统计辅助
 * ============================================================
 */

u32 buddy_nr_free_blocks(unsigned int order) {
  if (!valid_order(order))
    return 0;
  return free_area[order].nr_free;
}

u32 buddy_nr_free_pages_total(void) {
  unsigned int order;
  u32 total = 0;

  for (order = 0; order <= MAX_ORDER; order++) {
    total += free_area[order].nr_free * (1U << order);
  }

  return total;
}

/*
 * 可选：返回指定物理地址是否当前处于"块首页已分配"状态
 * 便于测试。
 */
int buddy_is_allocated_head(phys_addr_t pa) {
  unsigned long pfn;
  struct page *page;

  if (!pa_in_range(pa) || !page_aligned(pa))
    return 0;

  pfn = pa_to_pfn(pa);
  if (pfn >= TOTAL_PAGES)
    return 0;

  page = pfn_to_page(pfn);
  return page_is_allocated(page) && (page->flags & PG_HEAD);
}

/* ============================================================
 * 8. 页管理辅助函数
 * ============================================================
 */

/**
 * alloc_page - 分配一页物理内存
 * @flags: 分配标志 (GFP_*)
 *
 * 返回：
 *  - 成功：page 结构体指针
 *  - 失败：NULL
 */
struct page *alloc_page(gfp_t flags) {
    // 使用alloc_phys_pages分配一页物理内存
    phys_addr_t pa = alloc_phys_pages(0, flags);
    if (!pa) return NULL;
    // 转换为page结构体指针
    return pfn_to_page(pa_to_pfn(pa));
}

/**
 * free_page - 释放一页物理内存
 * @page: page 结构体指针
 */
void free_page(struct page *page) {
    // 转换为物理地址
    unsigned long pfn = page_to_pfn(page);
    phys_addr_t pa = pfn_to_pa(pfn);
    // 释放物理内存
    free_phys_pages(pa, 0);
}

/* ============================================================
 * 9. 内存使用统计函数
 * ============================================================
 */

/**
 * buddy_nr_used_pages_total - 返回当前已使用的页数
 */
unsigned int buddy_nr_used_pages_total(void) {
  return TOTAL_PAGES - buddy_nr_free_pages_total();
}

/**
 * buddy_mem_usage_percent - 返回内存占用率（百分比）
 */
unsigned int buddy_mem_usage_percent(void) {
  if (TOTAL_PAGES == 0)
    return 0;
  return (buddy_nr_used_pages_total() * 100) / TOTAL_PAGES;
}

/**
 * total_phys_pages - 返回总物理页数（包括内核占用部分）
 */
unsigned long total_phys_pages(void) {
  return (PHYS_MEM_END - PHYS_MEM_START + 1) / PAGE_SIZE;
}

/**
 * total_used_pages - 返回总已使用页数（包括内核占用部分）
 */
unsigned long total_used_pages(void) {
  // 内核占用的页数 = (BUDDY_MEM_START - PHYS_MEM_START) / PAGE_SIZE
  unsigned long kernel_pages = (BUDDY_MEM_START - PHYS_MEM_START) / PAGE_SIZE;
  // 总已使用页数 = 内核占用的页数 + buddy 已使用的页数
  return kernel_pages + buddy_nr_used_pages_total();
}

/**
 * total_mem_usage_percent - 返回总物理内存占用率（百分比）
 */
unsigned int total_mem_usage_percent(void) {
  unsigned long total_pages = total_phys_pages();
  if (total_pages == 0)
    return 0;
  return (total_used_pages() * 100) / total_pages;
}