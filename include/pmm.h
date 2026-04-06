#ifndef BUDDY_H
#define BUDDY_H

typedef unsigned long phys_addr_t;

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