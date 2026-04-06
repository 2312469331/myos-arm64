/*
 * buddy.c - ARM64 bare-metal buddy physical memory allocator
 *
 * 特点：
 *  - 4KB 页大小
 *  - 管理物理地址范围默认：0x80000000 ~ 0x8FFFFFFF
 *  - order: 0 ~ 10（1页 ~ 1024页）
 *  - 纯 C，无 libc、无 OS 依赖
 *  - 适用于裸机内核物理页管理：页表、DMA、内核堆底层页分配
 *
 * 说明：
 *  - alloc_phys_pages(order) 返回连续物理页块起始物理地址
 *  - free_phys_pages(pa, order) 释放对应块，并自动尝试与伙伴合并
 *  - 失败时 alloc 返回 0
 *
 * 作者说明：
 *  本实现强调教学可读性，适合 OS 学习与裸机实验。
 */

typedef unsigned long       u64;
typedef unsigned int        u32;
typedef unsigned short      u16;
typedef unsigned char       u8;
typedef unsigned long       phys_addr_t;
typedef unsigned long       size_t;

/* ============================================================
 * 1. 可配置参数
 * ============================================================
 */

#define PAGE_SHIFT          12UL
#define PAGE_SIZE           (1UL << PAGE_SHIFT)   /* 4096 bytes */
#define PAGE_MASK           (~(PAGE_SIZE - 1UL))

/* 可修改的物理内存管理范围 */
#define PHYS_MEM_START      0x40200000UL
#define PHYS_MEM_END        0x4FFFFFFFUL

/* order 0 ~ 10 : 1页 ~ 1024页 */
#define MAX_ORDER           10U
#define NR_ORDERS           (MAX_ORDER + 1U)

/*
 * 总页数：
 * 0x80000000 ~ 0x8FFFFFFF 为 256MB
 * 256MB / 4KB = 65536 pages
 */
#define PHYS_MEM_SIZE       (PHYS_MEM_END - PHYS_MEM_START + 1UL)
#define TOTAL_PAGES         (PHYS_MEM_SIZE >> PAGE_SHIFT)

/*
 * 基本健壮性检查：
 * 若区间不是页对齐，会向下/向上裁剪才合理。
 * 这里直接要求宏配置满足页对齐。
 */
#if ((PHYS_MEM_START & (PAGE_SIZE - 1UL)) != 0)
#error "PHYS_MEM_START must be page aligned"
#endif

#if (((PHYS_MEM_END + 1UL) & (PAGE_SIZE - 1UL)) != 0)
#error "PHYS_MEM_END+1 must be page aligned"
#endif

/* ============================================================
 * 2. 双向链表
 * ============================================================
 */

struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

static void INIT_LIST_HEAD(struct list_head *list)
{
    list->next = list;
    list->prev = list;
}

static int list_empty(const struct list_head *head)
{
    return (head->next == head);
}

static void __list_add(struct list_head *node,
                       struct list_head *prev,
                       struct list_head *next)
{
    next->prev = node;
    node->next = next;
    node->prev = prev;
    prev->next = node;
}

static void list_add(struct list_head *node, struct list_head *head)
{
    /* 插入到头部后面 */
    __list_add(node, head, head->next);
}

static void list_add_tail(struct list_head *node, struct list_head *head)
{
    /* 插入到尾部前面 */
    __list_add(node, head->prev, head);
}

static void __list_del(struct list_head *prev, struct list_head *next)
{
    next->prev = prev;
    prev->next = next;
}

static void list_del(struct list_head *entry)
{
    __list_del(entry->prev, entry->next);
    entry->next = entry;
    entry->prev = entry;
}

/* container_of 的简单替代 */
#define offsetof(TYPE, MEMBER) ((size_t) &(((TYPE *)0)->MEMBER))
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* ============================================================
 * 3. 页面元数据
 * ============================================================
 */

/*
 * page.flags 定义
 */
#define PG_RESERVED     (1UL << 0)  /* 保留，不可分配 */
#define PG_BUDDY        (1UL << 1)  /* 当前页是某个空闲块的首页，已挂到 buddy 空闲链表 */
#define PG_ALLOCATED    (1UL << 2)  /* 块已分配（仅首页使用此标志即可） */
#define PG_HEAD         (1UL << 3)  /* 表示这是一个块首页 */

/*
 * struct page
 *
 * order   : 当前块的阶，仅对块首页有意义
 * private : 预留字段，这里可存调试信息/页框索引/所属用途等
 * flags   : 状态位
 * node    : 挂入 free_area[order] 的链表节点
 */
struct page {
    u16 order;
    u16 private;
    u32 flags;
    struct list_head node;
};

struct free_area {
    struct list_head free_list;
    u32 nr_free;   /* 当前 order 的空闲块数量 */
};

/* 所有页的元数据数组 */
static struct page mem_map[TOTAL_PAGES];

/* 多级空闲链表 */
static struct free_area free_area[NR_ORDERS];

/* ============================================================
 * 4. 错误处理
 * ============================================================
 */

enum buddy_error {
    BUDDY_OK = 0,
    BUDDY_ERR_BAD_ORDER,
    BUDDY_ERR_OUT_OF_RANGE,
    BUDDY_ERR_UNALIGNED,
    BUDDY_ERR_DOUBLE_FREE,
    BUDDY_ERR_NOT_ALLOCATED,
    BUDDY_ERR_NO_MEMORY,
};

static volatile enum buddy_error buddy_last_error = BUDDY_OK;

enum buddy_error buddy_get_last_error(void)
{
    return buddy_last_error;
}

static void buddy_set_error(enum buddy_error err)
{
    buddy_last_error = err;
}

/* ============================================================
 * 5. 地址与页框号转换
 * ============================================================
 */

static unsigned long pa_to_pfn(phys_addr_t pa)
{
    return (unsigned long)((pa - PHYS_MEM_START) >> PAGE_SHIFT);
}

static phys_addr_t pfn_to_pa(unsigned long pfn)
{
    return (phys_addr_t)(PHYS_MEM_START + (pfn << PAGE_SHIFT));
}

static int pa_in_range(phys_addr_t pa)
{
    return (pa >= PHYS_MEM_START) && (pa <= PHYS_MEM_END);
}

static int valid_order(unsigned int order)
{
    return (order <= MAX_ORDER);
}

static int page_aligned(phys_addr_t pa)
{
    return ((pa & (PAGE_SIZE - 1UL)) == 0);
}

/* ============================================================
 * 6. page 状态辅助函数
 * ============================================================
 */

static struct page *pfn_to_page(unsigned long pfn)
{
    return &mem_map[pfn];
}

static int page_is_buddy(struct page *page)
{
    return (page->flags & PG_BUDDY) != 0;
}

static int page_is_allocated(struct page *page)
{
    return (page->flags & PG_ALLOCATED) != 0;
}

static int page_is_reserved(struct page *page)
{
    return (page->flags & PG_RESERVED) != 0;
}

static void set_page_order(struct page *page, unsigned int order)
{
    page->order = (u16)order;
}

static void clear_page_order(struct page *page)
{
    page->order = 0;
}

static void mark_page_buddy(struct page *page, unsigned int order)
{
    page->flags = PG_BUDDY | PG_HEAD;
    page->order = (u16)order;
}

static void mark_page_allocated(struct page *page, unsigned int order)
{
    page->flags = PG_ALLOCATED | PG_HEAD;
    page->order = (u16)order;
}

static void mark_page_reserved(struct page *page)
{
    page->flags = PG_RESERVED;
    page->order = 0;
}

static void clear_page_flags(struct page *page)
{
    page->flags = 0;
    page->order = 0;
}

/* ============================================================
 * 7. buddy 核心辅助逻辑
 * ============================================================
 */

/*
 * 伙伴页框号计算：
 * 对于 order = k，块大小 = 2^k 页
 * buddy_pfn = pfn ^ (1 << k)
 */
static unsigned long buddy_pfn(unsigned long pfn, unsigned int order)
{
    return pfn ^ (1UL << order);
}

/*
 * 判断给定 pfn 是否能作为某个 order 块的首页
 */
static int pfn_aligned_to_order(unsigned long pfn, unsigned int order)
{
    unsigned long pages = (1UL << order);
    return ((pfn & (pages - 1UL)) == 0);
}

/*
 * 向 free_area[order] 插入一个空闲块
 */
static void add_to_free_area(unsigned long pfn, unsigned int order)
{
    struct page *page = pfn_to_page(pfn);

    mark_page_buddy(page, order);
    list_add_tail(&page->node, &free_area[order].free_list);
    free_area[order].nr_free++;
}

/*
 * 从 free_area[order] 删除一个空闲块
 */
static void remove_from_free_area(unsigned long pfn, unsigned int order)
{
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
static unsigned long expand_block(unsigned long pfn,
                                  unsigned int high,
                                  unsigned int low)
{
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
static unsigned long buddy_alloc_pfn(unsigned int order)
{
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
static unsigned long buddy_merge_pfn(unsigned long pfn, unsigned int *order)
{
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
 * 8. 初始化
 * ============================================================
 */

/*
 * 将整个管理区域按“尽可能大的且地址对齐的块”加入 buddy 系统
 *
 * 这样既保证：
 *  - 全部内存都被纳入管理
 *  - 初始碎片最少
 *
 * 举例：
 * 如果从 0 页开始，通常会优先放入大量 order=10 块
 * 剩余尾部再用更小 order 补齐。
 */
void buddy_init(void)
{
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
     * 如果你有内核镜像、自举页表、设备保留区等，
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

        /* 非首页页仅保留“已被系统管理但不是块头”的普通状态 */
        for (i = 1; i < (1UL << order); i++) {
            clear_page_flags(&mem_map[pfn + i]);
        }

        pfn += (1UL << order);
        remain -= (1UL << order);
    }
}

/* ============================================================
 * 9. 对外分配接口
 * ============================================================
 */

/*
 * alloc_phys_pages - 分配 2^order 个连续物理页
 *
 * 返回：
 *  - 成功：起始物理地址
 *  - 失败：0
 */
phys_addr_t alloc_phys_pages(unsigned int order)
{
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
 * 10. 对外释放接口
 * ============================================================
 */

/*
 * free_phys_pages - 释放 2^order 个连续物理页
 *
 * 注意：
 *  - pa 必须是当初 alloc 返回的块起始地址
 *  - order 必须与分配时一致
 */
void free_phys_pages(phys_addr_t pa, unsigned int order)
{
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
 * 11. 调试/统计辅助
 * ============================================================
 */

u32 buddy_nr_free_blocks(unsigned int order)
{
    if (!valid_order(order))
        return 0;
    return free_area[order].nr_free;
}

u32 buddy_nr_free_pages_total(void)
{
    unsigned int order;
    u32 total = 0;

    for (order = 0; order <= MAX_ORDER; order++) {
        total += free_area[order].nr_free * (1U << order);
    }

    return total;
}

/*
 * 可选：返回指定物理地址是否当前处于“块首页已分配”状态
 * 便于测试。
 */
int buddy_is_allocated_head(phys_addr_t pa)
{
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
