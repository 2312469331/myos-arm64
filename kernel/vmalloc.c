#include <vmalloc.h>
#include <slab.h>      // 假设你有 kmalloc/kfree
#include <printk.h>
#include <libc.h>
#include <mm_defs.h>
#include <pmm.h>
// 假设你的底层有这些函数(需你自行实现)：
// extern struct page *alloc_page(int flags);
// extern void free_page(struct page *page);
// extern void map_kernel_page(uint64_t va, uint64_t pa, uint64_t prot);
// extern void unmap_kernel_page(uint64_t va);
#include <mmu.h>       // 包含页表操作函数

// 实现所需的底层函数
struct page *alloc_page(int flags) {
    // 使用alloc_phys_pages分配一页物理内存
    phys_addr_t pa = alloc_phys_pages(0, GFP_KERNEL);
    if (!pa) return NULL;
    // 转换为page结构体指针
    return pfn_to_page(pa_to_pfn(pa));
}

void free_page(struct page *page) {
    // 转换为物理地址
    unsigned long pfn = page_to_pfn(page);
    phys_addr_t pa = pfn_to_pa(pfn);
    // 释放物理内存
    free_phys_pages(pa, 0);
}

void map_kernel_page(uint64_t va, uint64_t pa, uint64_t prot) {
    // 使用arm64_map_one_page映射页表
    arm64_map_one_page(va, pa, prot);
}

void unmap_kernel_page(uint64_t va) {
    // 使用arm64_unmap_one_page解除映射
    arm64_unmap_one_page(va);
}



// 页表属性宏 (假设你的架构是这样定义的，按需修改)
#define PAGE_KERNEL_RW  0x03  // 可读写，普通内存
#define PAGE_KERNEL_RX  0x05  // 可读可执行，模块用
#define PAGE_DEVICE     0x11  // 设备内存，不可缓存

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
    struct vm_struct *vm = kmalloc(sizeof(struct vm_struct));
    void **pages_array = kmalloc(sizeof(void *) * nr_pages);
    if (!vm || !pages_array) goto fail;

    for (i = 0; i < nr_pages; i++) {
        // 调用你写好的物理页分配器
        struct page *page = alloc_page(0); 
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
// 特殊业务：ioremap (不走物理页分配器)
// ==========================================
void *ioremap(uint64_t phys_addr, unsigned long size) {
    // 1. 买地皮 (注意标志位不同)
    void *addr = va_alloc(size, PAGE_SIZE, VA_FLAG_IOREMAP);
    if (!addr) return NULL;

    // 2. 直接用物理地址建页表，不调 alloc_page！
    unsigned long nr_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t va_start = (uint64_t)addr;
    for (unsigned long i = 0; i < nr_pages; i++) {
        map_kernel_page(va_start + i * PAGE_SIZE, phys_addr + i * PAGE_SIZE, PAGE_DEVICE);
    }

    // ioremap 不需要 vm_struct 记录物理页，因为物理页不是我们分配的，iounmap 时不能 free
    // (为了严谨，其实 Linux 也是有单独的 vmap 管理机制来追踪 ioremap，但最简实现可以先这样)
    return addr;
}

void iounmap(const void *addr) {
    if (!addr) return;
    // 简化处理：找到对应的 vmap_area 算出大小，拆页表，然后 va_free
    // 实际上你需要根据 addr 算出大小，或者把 ioremap 的 size 也记在某个地方
    // 这里仅演示核心逻辑
    // unmap_kernel_page(...);
    // va_free(addr);
}

