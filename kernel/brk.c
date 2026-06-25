#include <brk.h>
#include <slab.h>
#include <gfp.h>
#include <printk.h>
#include <mmu.h>
#include <pgtbl.h>

// brk 堆管理：扩展或收缩堆
// 返回新 brk 地址，失败返回旧 brk
uint64_t do_brk(struct mm_struct *mm, uint64_t new_brk) {
    if (!mm) return 0;

    // 页对齐
    new_brk = (new_brk + PAGE_SIZE - 1) & ~PAGE_MASK;

    if (new_brk == mm->brk) return mm->brk;

    unsigned long irq_flags;
    spin_lock_irqsave(&mm->lock, irq_flags);

    if (new_brk > mm->brk) {
        // 扩展堆：映射新页
        uint64_t addr = mm->brk;
        while (addr < new_brk) {
            // 分配物理页
            phys_addr_t pa = alloc_phys_pages(0, GFP_KERNEL);
            if (!pa) {
                spin_unlock_irqrestore(&mm->lock, irq_flags);
                return mm->brk;
            }
            // 清零
            void *va = phys_to_virt(pa);
            for (unsigned long i = 0; i < PAGE_SIZE; i++) {
                ((char *)va)[i] = 0;
            }
            // 映射到用户页表
            int ret = map_page(addr, pa, PAGE_USER_RW);
            if (ret != 0) {
                free_phys_pages(pa, 0);
                spin_unlock_irqrestore(&mm->lock, irq_flags);
                return mm->brk;
            }
            addr += PAGE_SIZE;
        }
    } else if (new_brk < mm->brk) {
        // 收缩堆：解映射并释放物理页
        uint64_t addr = new_brk;
        while (addr < mm->brk) {
            phys_addr_t pa = get_phys_from_va(addr);
            unmap_page(addr);
            if (pa) {
                free_phys_pages(pa, 0);
            }
            addr += PAGE_SIZE;
        }
    }

    mm->brk = new_brk;
    spin_unlock_irqrestore(&mm->lock, irq_flags);
    return mm->brk;
}
