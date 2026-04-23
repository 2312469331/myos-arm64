#ifndef BUDDY_H
#define BUDDY_H

#include <mm_defs.h>
#include <ds/list.h>
#include <gfp.h>
/* ============================================================
 * 1. 可配置参数
 * ============================================================
 */

/* order 0 ~ 10 : 1页 ~ 1024页 */
#define MAX_ORDER 10U
#define NR_ORDERS (MAX_ORDER + 1U)

/*
 * 总页数：
 * 0x80000000 ~ 0x8FFFFFFF 为 256MB
 * 256MB / 4KB = 65536 pages
 */
#define PHYS_MEM_SIZE (BUDDY_MEM_END - BUDDY_MEM_START + 1UL)
#define TOTAL_PAGES (PHYS_MEM_SIZE >> PAGE_SHIFT)

/*
 * 基本健壮性检查：
 * 若区间不是页对齐，会向下/向上裁剪才合理。
 * 这里直接要求宏配置满足页对齐。
 */
#if ((BUDDY_MEM_START & (PAGE_SIZE - 1UL)) != 0)
#error "BUDDY_MEM_START must be page aligned"
#endif

#if (((BUDDY_MEM_END + 1UL) & (PAGE_SIZE - 1UL)) != 0)
#error "BUDDY_MEM_END+1 must be page aligned"
#endif

/* ============================================================
 * 2. page.flags 宏定义（供所有内存管理模块使用）
 * ============================================================
 */
#define PG_RESERVED (1UL << 0) /* 保留，不可分配 */
#define PG_BUDDY    (1UL << 1) /* 当前页是某个空闲块的首页，已挂到 buddy 空闲链表 */
#define PG_ALLOCATED (1UL << 2) /* 块已分配（仅首页使用此标志即可） */
#define PG_HEAD (1UL << 3)      /* 表示这是一个块首页 */

/* ============================================================
 * 3. 外部变量声明（mem_map 在 pmm.c 中定义）
 * ============================================================
 */
extern struct page mem_map[TOTAL_PAGES];

/* ============================================================
 * 4. page 状态辅助函数（供其他内存管理模块使用）
 * ============================================================
 */

/* 页框号与 page 结构体互转 */
static inline struct page *pfn_to_page(unsigned long pfn) { return &mem_map[pfn]; }
static inline unsigned long page_to_pfn(struct page *page) { return (unsigned long)(page - mem_map); }

/* 地址与 PFN 互转 */
static inline phys_addr_t pfn_to_pa(unsigned long pfn) { return (phys_addr_t)(BUDDY_MEM_START + (pfn << PAGE_SHIFT)); }
static inline unsigned long pa_to_pfn(phys_addr_t pa) { return (unsigned long)((pa - BUDDY_MEM_START) >> PAGE_SHIFT); }

/* page 状态检查 */
static inline int page_is_buddy(struct page *page) { return (page->flags & PG_BUDDY) != 0; }
static inline int page_is_allocated(struct page *page) { return (page->flags & PG_ALLOCATED) != 0; }
static inline int page_is_reserved(struct page *page) { return (page->flags & PG_RESERVED) != 0; }

/* page 状态设置 */
static inline void set_page_order(struct page *page, unsigned int order) { page->order = (u16)order; }
static inline void clear_page_order(struct page *page) { page->order = 0; }

static inline void mark_page_buddy(struct page *page, unsigned int order) {
  page->flags = PG_BUDDY | PG_HEAD;
  page->order = (u16)order;
}

static inline void mark_page_allocated(struct page *page, unsigned int order) {
  page->flags = PG_ALLOCATED | PG_HEAD;
  page->order = (u16)order;
}

static inline void mark_page_reserved(struct page *page) {
  page->flags = PG_RESERVED;
  page->order = 0;
}

static inline void clear_page_flags(struct page *page) {
  page->flags = 0;
  page->order = 0;
}

/* ============================================================
 * 5. 双向链表
 * ============================================================
 */

void buddy_init(void);
phys_addr_t alloc_phys_pages(unsigned int order,gfp_t flags);
void free_phys_pages(phys_addr_t pa, unsigned int order);

unsigned int buddy_nr_free_blocks(unsigned int order);
unsigned int buddy_nr_free_pages_total(void);
unsigned int buddy_nr_used_pages_total(void);
unsigned int buddy_mem_usage_percent(void);
unsigned long total_phys_pages(void);
unsigned long total_used_pages(void);
unsigned int total_mem_usage_percent(void);

enum buddy_error {
  BUDDY_OK = 0,
  BUDDY_ERR_BAD_ORDER,
  BUDDY_ERR_NO_MEMORY,
  BUDDY_ERR_OUT_OF_RANGE,
  BUDDY_ERR_UNALIGNED,
  BUDDY_ERR_NOT_ALLOCATED,
  BUDDY_ERR_DOUBLE_FREE,
};

/* ============================================================
 * 页管理辅助函数
 * ============================================================
 */

struct page *alloc_page(gfp_t flags);
void free_page(struct page *page);
void map_kernel_page(uint64_t va, uint64_t pa, uint64_t prot);
void unmap_kernel_page(uint64_t va);

enum buddy_error buddy_get_last_error(void);

#endif