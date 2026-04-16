#include "mmu.h"
#include "types.h"

/* 全局变量定义 */
uintptr_t slab_linear_map_base;
phys_addr_t slab_l0_table_pa;
