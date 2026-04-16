#include <vmalloc.h>
#include <mmu.h>
#include <pmm.h>
#include <pgtbl.h>
#include <printk.h>
#include <slab.h>
#include <libc.h>

/* =========================================================
 * 全局变量
 * =========================================================
 */

static struct list_head vmalloc_list;
static spinlock_t vmalloc_lock;
static uint64_t vmalloc_next_addr = VMALLOC_START;

/* 内核地址空间 */
struct mm_struct init_mm;

/* 导出给 page_fault.c 使用 */
spinlock_t *get_vmalloc_lock(void) {
    return &vmalloc_lock;
}

/* =========================================================
 * 辅助函数
 * =========================================================
 */

static inline uint64_t align_up(uint64_t addr, uint64_t size) {
    return (addr + size - 1) & ~(size - 1);
}

/* =========================================================
 * vmalloc 区域管理
 * =========================================================
 */

static struct vmalloc_area *find_vmalloc_area(uint64_t addr) {
    struct vmalloc_area *area;
    
    list_for_each_entry(area, &vmalloc_list, list) {
        if (addr >= area->start && addr < area->start + area->size) {
            return area;
        }
    }
    return NULL;
}

static int is_vmalloc_area_available(uint64_t start, size_t size) {
    struct vmalloc_area *area;
    uint64_t end = start + size;
    
    list_for_each_entry(area, &vmalloc_list, list) {
        uint64_t area_end = area->start + area->size;
        
        if (!(end <= area->start || start >= area_end)) {
            return 0;
        }
    }
    return 1;
}

static uint64_t find_vmalloc_addr(size_t size) {
    uint64_t addr;
    uint64_t aligned_size = align_up(size, PAGE_SIZE);
    
    addr = align_up(vmalloc_next_addr, PAGE_SIZE);
    
    while (addr + aligned_size <= VMALLOC_END) {
        if (is_vmalloc_area_available(addr, aligned_size)) {
            vmalloc_next_addr = addr + aligned_size;
            return addr;
        }
        addr += aligned_size;
    }
    
    return 0;
}

/* =========================================================
 * vmalloc 区域查找（供 page_fault 使用）
 * =========================================================
 */

struct vmalloc_area *find_vmalloc_area_unlocked(uint64_t addr) {
    struct vmalloc_area *area;
    
    list_for_each_entry(area, &vmalloc_list, list) {
        if (addr >= area->start && addr < area->start + area->size) {
            return area;
        }
    }
    return NULL;
}

/* =========================================================
 * vmalloc 核心实现
 * =========================================================
 */

void *vmalloc(size_t size) {
    struct vmalloc_area *area;
    uint64_t addr;
    unsigned int nr_pages;
    unsigned int i;
    
    if (size == 0) {
        return NULL;
    }
    
    nr_pages = (align_up(size, PAGE_SIZE)) >> PAGE_SHIFT;
    
    area = (struct vmalloc_area *)kmalloc(sizeof(struct vmalloc_area));
    if (!area) {
        printk("vmalloc: failed to allocate vmalloc_area\n");
        return NULL;
    }
    
    addr = find_vmalloc_addr(size);
    if (!addr) {
        printk("vmalloc: no available virtual address space\n");
        kfree(area);
        return NULL;
    }
    
    area->pages = (phys_addr_t *)kmalloc(sizeof(phys_addr_t) * nr_pages);
    if (!area->pages) {
        printk("vmalloc: failed to allocate pages array\n");
        kfree(area);
        return NULL;
    }
    
    for (i = 0; i < nr_pages; i++) {
        phys_addr_t pa = alloc_phys_pages(0);
        if (pa == 0) {
            printk("vmalloc: failed to allocate physical page %u\n", i);
            while (i-- > 0) {
                free_phys_pages(area->pages[i], 0);
            }
            kfree(area->pages);
            kfree(area);
            return NULL;
        }
        area->pages[i] = pa;
    }
    
    for (i = 0; i < nr_pages; i++) {
        uint64_t va = addr + (i * PAGE_SIZE);
        if (arm64_map_one_page(va, area->pages[i]) != 0) {
            printk("vmalloc: failed to map page at 0x%llx\n", va);
            while (i-- > 0) {
                uint64_t va = addr + (i * PAGE_SIZE);
                arm64_unmap_one_page(va);
            }
            for (i = 0; i < nr_pages; i++) {
                free_phys_pages(area->pages[i], 0);
            }
            kfree(area->pages);
            kfree(area);
            return NULL;
        }
    }
    
    area->start = addr;
    area->size = align_up(size, PAGE_SIZE);
    area->flags = VMALLOC_MAP;
    area->nr_pages = nr_pages;
    area->caller = "vmalloc";
    
    spin_lock(&vmalloc_lock);
    list_add(&area->list, &vmalloc_list);
    spin_unlock(&vmalloc_lock);
    
    printk("vmalloc: allocated 0x%llx bytes at 0x%llx\n", size, addr);
    
    return (void *)addr;
}

void vfree(const void *addr) {
    struct vmalloc_area *area;
    uint64_t va = (uint64_t)addr;
    unsigned int i;
    
    if (!addr) {
        return;
    }
    
    spin_lock(&vmalloc_lock);
    area = find_vmalloc_area(va);
    if (!area) {
        spin_unlock(&vmalloc_lock);
        printk("vfree: invalid address 0x%llx\n", va);
        return;
    }
    list_del(&area->list);
    spin_unlock(&vmalloc_lock);
    
    for (i = 0; i < area->nr_pages; i++) {
        uint64_t page_va = area->start + (i * PAGE_SIZE);
        arm64_unmap_one_page(page_va);
        free_phys_pages(area->pages[i], 0);
    }
    
    printk("vfree: freed 0x%llx bytes at 0x%llx\n", area->size, area->start);
    
    kfree(area->pages);
    kfree(area);
}

void *vmalloc_lazy(size_t size) {
    struct vmalloc_area *area;
    uint64_t addr;
    unsigned int nr_pages;
    unsigned int i;
    
    if (size == 0) {
        return NULL;
    }
    
    nr_pages = (align_up(size, PAGE_SIZE)) >> PAGE_SHIFT;
    
    area = (struct vmalloc_area *)kmalloc(sizeof(struct vmalloc_area));
    if (!area) {
        printk("vmalloc_lazy: failed to allocate vmalloc_area\n");
        return NULL;
    }
    
    addr = find_vmalloc_addr(size);
    if (!addr) {
        printk("vmalloc_lazy: no available virtual address space\n");
        kfree(area);
        return NULL;
    }
    
    area->pages = (phys_addr_t *)kmalloc(sizeof(phys_addr_t) * nr_pages);
    if (!area->pages) {
        printk("vmalloc_lazy: failed to allocate pages array\n");
        kfree(area);
        return NULL;
    }
    
    for (i = 0; i < nr_pages; i++) {
        phys_addr_t pa = alloc_phys_pages(0);
        if (pa == 0) {
            printk("vmalloc_lazy: failed to allocate physical page %u\n", i);
            while (i-- > 0) {
                free_phys_pages(area->pages[i], 0);
            }
            kfree(area->pages);
            kfree(area);
            return NULL;
        }
        area->pages[i] = pa;
    }
    
    area->start = addr;
    area->size = align_up(size, PAGE_SIZE);
    area->flags = VMALLOC_LAZY;
    area->nr_pages = nr_pages;
    area->caller = "vmalloc_lazy";
    
    spin_lock(&vmalloc_lock);
    list_add(&area->list, &vmalloc_list);
    spin_unlock(&vmalloc_lock);
    
    printk("vmalloc_lazy: allocated 0x%llx bytes at 0x%llx (lazy mapping)\n", size, addr);
    
    return (void *)addr;
}

/* =========================================================
 * vmalloc 初始化和管理
 * =========================================================
 */

void vmalloc_init(void) {
    INIT_LIST_HEAD(&vmalloc_list);
    spin_lock_init(&vmalloc_lock);
    vmalloc_next_addr = VMALLOC_START;
    
    init_mm.start_code = KERNEL_SPACE_START;
    init_mm.end_code = KERNEL_SPACE_START + 0x1000000;
    init_mm.start_data = init_mm.end_code;
    init_mm.end_data = init_mm.start_data + 0x1000000;
    init_mm.start_brk = init_mm.end_data;
    init_mm.brk = init_mm.start_brk;
    init_mm.start_stack = KERNEL_SPACE_START + 0x10000000;
    init_mm.mmap = NULL;
    init_mm.page_table = NULL;
    spin_lock_init(&init_mm.page_table_lock);
    
    printk("vmalloc: initialized\n");
    printk("vmalloc: VMALLOC_START = 0x%llx\n", VMALLOC_START);
    printk("vmalloc: VMALLOC_END = 0x%llx\n", VMALLOC_END);
}

void vmalloc_area_init(struct vmalloc_area *area, uint64_t start, 
                      size_t size, uint64_t flags) {
    area->start = start;
    area->size = size;
    area->flags = flags;
    area->pages = NULL;
    area->nr_pages = 0;
    INIT_LIST_HEAD(&area->list);
    area->caller = "manual";
}

/* =========================================================
 * vmalloc 查询和调试
 * =========================================================
 */

int vmalloc_get_info(const void *addr, struct vmalloc_area *info) {
    struct vmalloc_area *area;
    uint64_t va = (uint64_t)addr;
    
    spin_lock(&vmalloc_lock);
    area = find_vmalloc_area(va);
    if (!area) {
        spin_unlock(&vmalloc_lock);
        return -1;
    }
    
    if (info) {
        *info = *area;
    }
    spin_unlock(&vmalloc_lock);
    
    return 0;
}

void vmalloc_print_areas(void) {
    struct vmalloc_area *area;
    
    printk("=== vmalloc areas ===\n");
    
    spin_lock(&vmalloc_lock);
    list_for_each_entry(area, &vmalloc_list, list) {
        printk("  0x%llx - 0x%llx: size=0x%llx, pages=%u, flags=0x%llx\n",
               area->start, area->start + area->size,
               area->size, area->nr_pages, area->flags);
    }
    spin_unlock(&vmalloc_lock);
}
