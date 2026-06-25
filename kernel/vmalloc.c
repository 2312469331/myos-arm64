#include <vmalloc.h>
#include <slab.h>      // 假设有 kmalloc/kfree
#include <printk.h>
#include <libc.h>
#include <mm_defs.h>
#include <pmm.h>
#include <mmu.h>       // 包含页表操作函数
#include <sync/spinlock.h>

// 全局链表，记录所有已映射的 vm_struct
static LIST_HEAD(vm_list);
static spinlock_t vm_list_lock = SPIN_LOCK_INIT;

// ==========================================
// 第二层：装修总包 (核心枢纽函数)
// ==========================================
static void *__vmalloc_node_range(unsigned long size, unsigned long align,
                                  unsigned long va_flags, uint64_t prot)
{
    // 1. 找 vmap 买地皮 (纯虚拟地址)
    void *addr = va_alloc(size, align, va_flags);
    if (!addr) return NULL;

    unsigned long nr_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    unsigned long i;
    
    // 2. 买材料：分配物理页数组，以及真正的物理页
    struct vm_struct *vm = kmalloc(sizeof(struct vm_struct), GFP_KERNEL);
    void **pages_array = kmalloc(sizeof(void *) * nr_pages, GFP_KERNEL);
    if (!vm || !pages_array) goto fail;

    for (i = 0; i < nr_pages; i++) {
        // 调用写好的物理页分配器
        struct page *page = alloc_page(GFP_KERNEL); 
        if (!page) goto fail_cleanup_pages;
        pages_array[i] = (void *)page; // 直接存储page指针
    }

    // 3. 盖房子：建立页表映射
    uint64_t va_start = (uint64_t)addr;
    for (i = 0; i < nr_pages; i++) {
        struct page *page = (struct page *)pages_array[i];
        uint64_t pa = pfn_to_pa(page_to_pfn(page));
        map_kernel_page(va_start + i * PAGE_SIZE, pa, prot);
    }
    
    // 4. 备案：记入 vm_struct 方便以后 vfree 拆除
    vm->addr = addr;
    vm->size = size;
    vm->nr_pages = nr_pages;
    vm->pages = pages_array;
    vm->flags = va_flags;
    
    spin_lock(&vm_list_lock);
    list_add_tail(&vm->list, &vm_list);
    spin_unlock(&vm_list_lock);

    return addr;

fail_cleanup_pages:
    // 失败回滚：把已经分配的物理页退还
    for (unsigned long j = 0; j < i; j++) {
        free_page((struct page *)pages_array[j]);
    }
fail:
    if (pages_array) kfree(pages_array);
    if (vm) kfree(vm);
    // 地皮退还给 vmap
    va_free(addr);
    return NULL;
}

// ==========================================
// 第三层：对外暴露的 API 壳
// ==========================================

// 1. 普通内核虚拟内存分配
void *vmalloc(unsigned long size) {
    return __vmalloc_node_range(size, PAGE_SIZE, VA_FLAG_VMALLOC, PAGE_KERNEL_RW);
}

// 2. 分配并清零
void *vzalloc(unsigned long size) {
    void *addr = vmalloc(size);
    if (addr) memset(addr, 0, size);
    return addr;
}

// 3. 释放虚拟内存 (最难写的函数，因为要逆向拆解)
void vfree(const void *addr) {
    if (!addr) return;
    
    spin_lock(&vm_list_lock);
    struct vm_struct *vm;
    list_for_each_entry(vm, &vm_list, list) {
        if (vm->addr == addr) goto found;
    }
    spin_unlock(&vm_list_lock);
    printk("[VMALLOC] vfree: Invalid address %p\n", addr);
    return;

found:
    list_del(&vm->list); // 从备案链表摘除
    spin_unlock(&vm_list_lock);

    // 1. 拆房子：拆除页表映射
    uint64_t va_start = (uint64_t)addr;
    for (unsigned long i = 0; i < vm->nr_pages; i++) {
        unmap_kernel_page(va_start + i * PAGE_SIZE);
    }
    // 注意：这里可能需要 flush_tlb_kernel_range(va_start, va_start + vm->size);

    // 2. 退材料：释放所有物理页
    for (unsigned long i = 0; i < vm->nr_pages; i++) {
        free_page((struct page *)vm->pages[i]);
    }
    kfree(vm->pages);

    // 3. 退地皮：把虚拟地址还给 vmap 分配器
    va_free((void *)addr);
    kfree(vm);
}

// ==========================================
// 特殊业务：ioremap / iounmap
// 用链表追踪每次映射，确保 iounmap 能正确取消所有页
// ==========================================

struct io_mapping {
    struct list_head list;
    void *addr;              // va_alloc 返回的页对齐虚拟地址
    unsigned long nr_pages;  // 实际映射页数
};

static LIST_HEAD(io_map_list);
static spinlock_t io_map_lock = SPIN_LOCK_INIT;

void *ioremap(uint64_t phys_addr, unsigned long size) {
    uint64_t offset = phys_addr & (PAGE_SIZE - 1);
    uint64_t paddr_aligned = phys_addr & PAGE_MASK;
    unsigned long map_size = size + offset;

    void *addr = va_alloc(map_size, PAGE_SIZE, VA_FLAG_IOREMAP);
    if (!addr) return NULL;

    unsigned long nr_pages = (map_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t va_start = (uint64_t)addr;
    for (unsigned long i = 0; i < nr_pages; i++) {
        map_kernel_page(va_start + i * PAGE_SIZE,
                        paddr_aligned + i * PAGE_SIZE,
                        PAGE_DEVICE);
    }

    // 记录映射信息
    struct io_mapping *io = kmalloc(sizeof(struct io_mapping), GFP_KERNEL);
    if (io) {
        io->addr = addr;
        io->nr_pages = nr_pages;
        spin_lock(&io_map_lock);
        list_add(&io->list, &io_map_list);
        spin_unlock(&io_map_lock);
    }

    return (void *)(va_start + offset);
}
static LIST_HEAD(dma_map_list);
static spinlock_t dma_map_lock = SPIN_LOCK_INIT;

struct dma_mapping {
    struct list_head list;
    void *va;
    phys_addr_t pa;
    unsigned long nr_pages;
    unsigned int order;
};

/**
 * dma_alloc_coherent - 分配连续物理页并映射为 DMA 缓冲
 * @size: 所需大小（字节）
 * @dma_phys: 输出参数，返回物理地址
 * 返回：虚拟地址（Non-cacheable，CPU 和设备通过此地址访问）
 */
void *dma_alloc_coherent(unsigned long size, uint64_t *dma_phys) {
    unsigned long pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    unsigned int order = 0;
    while ((1UL << order) < pages) order++;

    phys_addr_t pa = alloc_phys_pages(order, GFP_DMA);
    if (!pa) return NULL;
    *dma_phys = pa;

    void *va = va_alloc(pages * PAGE_SIZE, PAGE_SIZE, VA_FLAG_IOREMAP);
    if (!va) {
        free_phys_pages(pa, order);
        return NULL;
    }

    uint64_t va_start = (uint64_t)va;
    for (unsigned long i = 0; i < pages; i++) {
        map_kernel_page(va_start + i * PAGE_SIZE, pa + i * PAGE_SIZE, PAGE_DMA);
    }

    struct dma_mapping *dma = kmalloc(sizeof(struct dma_mapping), GFP_KERNEL);
    if (dma) {
        dma->va = va;
        dma->pa = pa;
        dma->nr_pages = pages;
        dma->order = order;
        spin_lock(&dma_map_lock);
        list_add(&dma->list, &dma_map_list);
        spin_unlock(&dma_map_lock);
    }
    return va;
}

void dma_free_coherent(void *va) {
    if (!va) return;
    uint64_t va_aligned = (uint64_t)va & PAGE_MASK;

    spin_lock(&dma_map_lock);
    struct dma_mapping *dma = NULL;
    struct dma_mapping *pos;
    list_for_each_entry(pos, &dma_map_list, list) {
        if ((uint64_t)pos->va == va_aligned) {
            dma = pos;
            list_del(&pos->list);
            break;
        }
    }
    spin_unlock(&dma_map_lock);
    if (!dma) return;

    for (unsigned long i = 0; i < dma->nr_pages; i++) {
        unmap_kernel_page(va_aligned + i * PAGE_SIZE);
    }
    free_phys_pages(dma->pa, dma->order);
    kfree(dma);
}

void iounmap(volatile const void *addr) {
    if (!addr) return;

    uint64_t va = (uint64_t)addr;
    uint64_t va_aligned = va & PAGE_MASK;

    // 从链表查找原始映射页数
    unsigned long nr_pages = 1;
    spin_lock(&io_map_lock);
    struct io_mapping *io;
    list_for_each_entry(io, &io_map_list, list) {
        if (io->addr == (void *)va_aligned) {
            nr_pages = io->nr_pages;
            list_del(&io->list);
            kfree(io);
            break;
        }
    }
    spin_unlock(&io_map_lock);

    // 取消映射所有页
    for (unsigned long i = 0; i < nr_pages; i++) {
        unmap_kernel_page(va_aligned + i * PAGE_SIZE);
    }
    // 刷新 TLB
    asm volatile("dsb ish\n\tisb" ::: "memory");

    va_free((void *)va_aligned);
}
