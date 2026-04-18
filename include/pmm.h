#ifndef BUDDY_H
#define BUDDY_H

#include <mm_defs.h>
#include <ds/list.h>

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
 * 2. 双向链表
 * ============================================================
 */

void buddy_init(void);
phys_addr_t alloc_phys_pages(unsigned int order);
void free_phys_pages(phys_addr_t pa, unsigned int order);

unsigned int buddy_nr_free_blocks(unsigned int order);
unsigned int buddy_nr_free_pages_total(void);

enum buddy_error {
  BUDDY_OK = 0,
  BUDDY_ERR_BAD_ORDER,
  BUDDY_ERR_OUT_OF_RANGE,
  BUDDY_ERR_UNALIGNED,
  BUDDY_ERR_DOUBLE_FREE,
  BUDDY_ERR_NOT_ALLOCATED,
  BUDDY_ERR_NO_MEMORY,
};

enum buddy_error buddy_get_last_error(void);

#endif
