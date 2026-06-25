#include <mm.h>
#include <slab.h>
#include <gfp.h>
#include <printk.h>
#include <mmu.h>
#include <pgtbl.h>

// mmap 起始地址（用户空间内，避开 brk 和栈）
// 从 0xffff0000_10000000 开始分配
#define MMAP_BASE 0xffff000010000000UL

// 内核地址空间描述符
struct mm_struct init_mm = {
    .mmap = (void *)0,
    .start_brk = 0,
    .brk = 0,
    .start_stack = 0,
    .pgd = 0,
};

struct mm_struct *mm_create(void) {
    struct mm_struct *mm = (struct mm_struct *)kmalloc(sizeof(struct mm_struct), GFP_KERNEL);
    if (!mm) return (void *)0;
    mm->mmap = (void *)0;
    mm->start_brk = 0;
    mm->brk = 0;
    mm->start_stack = 0;
    mm->pgd = 0;
    spin_lock_init(&mm->lock);
    return mm;
}

void mm_destroy(struct mm_struct *mm) {
    if (!mm) return;
    struct vma_area *vma = mm->mmap;
    while (vma) {
        struct vma_area *next = vma->vm_next;
        kfree(vma);
        vma = next;
    }
    kfree(mm);
}

/**
 * mm_copy - 深复制 mm_struct 和所有 VMA 节点
 * @src: 源 mm_struct
 *
 * 返回新的 mm_struct 指针，失败返回 NULL。
 * 只复制描述符，不复制页表和物理页（由页表 COW 处理）。
 */
struct mm_struct *mm_copy(struct mm_struct *src) {
    if (!src) return (void *)0;

    struct mm_struct *dst = mm_create();
    if (!dst) return (void *)0;

    dst->start_brk = src->start_brk;
    dst->brk = src->brk;
    dst->start_stack = src->start_stack;
    dst->pgd = src->pgd;

    struct vma_area **pp = &dst->mmap;
    struct vma_area *svma = src->mmap;
    while (svma) {
        struct vma_area *dvma = (struct vma_area *)kmalloc(sizeof(struct vma_area), GFP_KERNEL);
        if (!dvma) {
            mm_destroy(dst);
            return (void *)0;
        }
        dvma->vm_start = svma->vm_start;
        dvma->vm_end = svma->vm_end;
        dvma->vm_flags = svma->vm_flags;
        dvma->vm_next = (void *)0;

        *pp = dvma;
        pp = &dvma->vm_next;
        svma = svma->vm_next;
    }

    return dst;
}

int vma_create(struct mm_struct *mm, uint64_t vm_start, uint64_t vm_end, unsigned long vm_flags) {
    if (!mm || vm_start >= vm_end) return -1;

    struct vma_area *vma = (struct vma_area *)kmalloc(sizeof(struct vma_area), GFP_KERNEL);
    if (!vma) return -1;

    vma->vm_start = vm_start;
    vma->vm_end = vm_end;
    vma->vm_flags = vm_flags;
    vma->vm_next = (void *)0;

    unsigned long irq_flags;
    spin_lock_irqsave(&mm->lock, irq_flags);

    // 插入链表，按地址递增排列
    struct vma_area **pp = &mm->mmap;
    while (*pp && (*pp)->vm_start < vm_start) {
        pp = &(*pp)->vm_next;
    }
    vma->vm_next = *pp;
    *pp = vma;

    spin_unlock_irqrestore(&mm->lock, irq_flags);
    return 0;
}

struct vma_area *vma_find(struct mm_struct *mm, uint64_t addr) {
    if (!mm) return (void *)0;
    struct vma_area *vma;
    for (vma = mm->mmap; vma; vma = vma->vm_next) {
        if (addr >= vma->vm_start && addr < vma->vm_end)
            return vma;
    }
    return (void *)0;
}

// 从 VMA 链表中移除并释放一个 VMA
static void vma_remove(struct mm_struct *mm, struct vma_area *vma) {
    if (!mm || !vma) return;
    unsigned long irq_flags;
    spin_lock_irqsave(&mm->lock, irq_flags);
    struct vma_area **pp = &mm->mmap;
    while (*pp && *pp != vma) {
        pp = &(*pp)->vm_next;
    }
    if (*pp) *pp = vma->vm_next;
    spin_unlock_irqrestore(&mm->lock, irq_flags);
    kfree(vma);
}

// 查找空闲虚拟地址范围
static uint64_t find_free_area(struct mm_struct *mm, uint64_t len) {
    uint64_t addr = MMAP_BASE;
    struct vma_area *vma;

    while (1) {
        vma = mm->mmap;
        int conflict = 0;
        while (vma) {
            if (addr < vma->vm_end && addr + len > vma->vm_start) {
                conflict = 1;
                addr = vma->vm_end;
                // 页对齐
                addr = (addr + PAGE_SIZE - 1) & ~PAGE_MASK;
                break;
            }
            vma = vma->vm_next;
        }
        if (!conflict) return addr;
    }
}

// mmap 匿名映射
uint64_t do_mmap(struct mm_struct *mm, uint64_t addr, uint64_t len, unsigned long flags) {
    if (!mm || len == 0) return 0;

    // 页对齐
    len = (len + PAGE_SIZE - 1) & ~PAGE_MASK;

    // 自动选择地址
    if (addr == 0) {
        addr = find_free_area(mm, len);
    }

    // 创建 VMA
    if (vma_create(mm, addr, addr + len, flags | VM_ANONYMOUS) != 0) {
        return 0;
    }

    // 分配物理页并映射
    uint64_t cur = addr;
    while (cur < addr + len) {
        phys_addr_t pa = alloc_phys_pages(0, GFP_KERNEL);
        if (!pa) {
            // 回滚：解映射已分配的页
            while (cur > addr) {
                cur -= PAGE_SIZE;
                phys_addr_t rpa = get_phys_from_va(cur);
                unmap_page(cur);
                if (rpa) put_page(rpa);
            }
            // 移除 VMA
            struct vma_area *vma = vma_find(mm, addr);
            if (vma) vma_remove(mm, vma);
            return 0;
        }
        // 清零
        void *va = phys_to_virt(pa);
        for (unsigned long i = 0; i < PAGE_SIZE; i++) {
            ((char *)va)[i] = 0;
        }
        // 映射
        int ret = map_page(cur, pa, PAGE_USER_RW);
        if (ret != 0) {
            free_phys_pages(pa, 0);
            while (cur > addr) {
                cur -= PAGE_SIZE;
                phys_addr_t rpa = get_phys_from_va(cur);
                unmap_page(cur);
                if (rpa) put_page(rpa);
            }
            struct vma_area *vma = vma_find(mm, addr);
            if (vma) vma_remove(mm, vma);
            return 0;
        }
        cur += PAGE_SIZE;
    }

    return addr;
}

// munmap：解映射并释放
int do_munmap(struct mm_struct *mm, uint64_t addr, uint64_t len) {
    if (!mm || len == 0) return -1;

    len = (len + PAGE_SIZE - 1) & ~PAGE_MASK;

    struct vma_area *vma = vma_find(mm, addr);
    if (!vma) return -1;

    unsigned long irq_flags;
    spin_lock_irqsave(&mm->lock, irq_flags);

    // 从链表摘除
    struct vma_area **pp = &mm->mmap;
    while (*pp && *pp != vma) {
        pp = &(*pp)->vm_next;
    }
    if (*pp) *pp = vma->vm_next;

    spin_unlock_irqrestore(&mm->lock, irq_flags);

    // 解映射页并释放物理页引用
    uint64_t cur = addr;
    while (cur < addr + len && cur < vma->vm_end) {
        phys_addr_t pa = get_phys_from_va(cur);
        unmap_page(cur);
        if (pa) put_page(pa);
        cur += PAGE_SIZE;
    }

    kfree(vma);
    return 0;
}
