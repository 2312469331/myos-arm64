#include <page_fault.h>
#include <mm.h>
#include <mmu.h>
#include <pgtbl.h>
#include <gfp.h>
#include <uart.h>
#include <vmap.h>

// 当前进程的 mm_struct（内核线程为 NULL）
struct mm_struct *current_mm = (void *)0;

// 无锁打印（异常上下文中不能用 printk）
static void pf_puts(const char *s) {
    while (*s) uart_putc(*s++);
}

// 处理 vmalloc 区域的 page fault（内核态延迟映射）
int vmalloc_handle_page_fault(uint64_t fault_addr) {
    // 检查地址是否在 vmalloc 范围内
    if (fault_addr < VMALLOC_START || fault_addr >= VMALLOC_START + VMALLOC_SIZE) {
        return -1;
    }

    // 查找包含 fault_addr 的 vmap_area
    // 通过 busy_list 遍历（简单实现）
    extern struct vmap_manager va_mgr;
    struct vmap_area *va;
    list_head_t *pos;
    list_for_each(pos, &va_mgr.busy_list) {
        va = list_entry(pos, struct vmap_area, busy_list);
        if (fault_addr >= va->va_start && fault_addr < va->va_end) {
            // 找到了，分配物理页并映射
            uint64_t page_addr = fault_addr & PAGE_MASK;
            phys_addr_t pa = alloc_phys_pages(0, GFP_KERNEL);
            if (!pa) {
                pf_puts("[PAGE FAULT] vmalloc: alloc failed\n");
                return -1;
            }
            // 清零
            void *va_kern = phys_to_virt(pa);
            for (unsigned long i = 0; i < PAGE_SIZE; i++) {
                ((char *)va_kern)[i] = 0;
            }
            // 映射到内核地址空间
            int ret = map_page(page_addr, pa, PAGE_KERNEL_RW);
            if (ret != 0) {
                free_phys_pages(pa, 0);
                pf_puts("[PAGE FAULT] vmalloc: map failed\n");
                return -1;
            }
            return 0;
        }
    }

    return -1; // 不在 vmalloc 范围内
}

void page_fault_handler(uint64_t fault_addr, uint64_t esr, uint64_t far) {
    // 先尝试 vmalloc 处理（内核态）
    if (vmalloc_handle_page_fault(fault_addr) == 0) {
        return;
    }

    struct mm_struct *mm = current_mm;
    if (!mm) {
        pf_puts("[PAGE FAULT] no mm\n");
        return;
    }

    /* rust_vma_find 返回 vm_flags（0=非法） */
    extern uint64_t rust_vma_find(void *mm, uint64_t addr);
    uint64_t vm_flags = rust_vma_find(mm, fault_addr);
    if (vm_flags == 0) {
        pf_puts("[PAGE FAULT] no vma at 0x");
        // 简单 hex 打印 fault_addr
        for (int i = 60; i >= 0; i -= 4) {
            int d = (fault_addr >> i) & 0xF;
            uart_putc(d < 10 ? '0' + d : 'a' + d - 10);
        }
        pf_puts(", sending SIGKILL\n");
        extern void rust_send_sigkill_current(void);
        rust_send_sigkill_current();
        return;
    }

    uint64_t page_addr = fault_addr & PAGE_MASK;

    /* ESR bit 6 = WnR: 0=读, 1=写 */
    int is_write = (esr >> 6) & 1;

    /* 检查是否已有映射（COW 场景：页存在但只读） */
    phys_addr_t old_pa = get_phys_from_va(fault_addr);

    if (old_pa != 0 && is_write) {
        /* 写只读页但 VMA 不允许写 → SIGSEGV */
        if (!(vm_flags & VM_WRITE)) {
            pf_puts("[PAGE FAULT] write fault on non-writable VMA, sending SIGSEGV\n");
            extern void rust_send_sigsegv_current(void);
            rust_send_sigsegv_current();
            return;
        }
        /* COW：写只读页 → 分配新页，复制内容，重新映射为可写 */
        phys_addr_t new_pa = alloc_phys_pages(0, GFP_KERNEL);
        if (!new_pa) {
            pf_puts("[PAGE FAULT] COW: alloc failed\n");
            return;
        }

        /* 复制旧页内容 */
        void *old_va = phys_to_virt(old_pa);
        void *new_va = phys_to_virt(new_pa);
        for (unsigned long i = 0; i < PAGE_SIZE; i++) {
            ((char *)new_va)[i] = ((char *)old_va)[i];
        }

        /* 解映射旧页，映射新页为可写 */
        unmap_page(page_addr);
        put_page(old_pa);
        int ret = map_page(page_addr, new_pa, PAGE_USER_RW);
        if (ret != 0) {
            free_phys_pages(new_pa, 0);
            pf_puts("[PAGE FAULT] COW: map failed\n");
            return;
        }
        flush_tlb();
        return;
    }

    /* 按需分配：页不存在，分配新页并清零 */
    /* 写缺页但 VMA 不允许写 → SIGSEGV */
    if (is_write && !(vm_flags & VM_WRITE)) {
        pf_puts("[PAGE FAULT] write fault on non-writable VMA (demand alloc), sending SIGSEGV\n");
        extern void rust_send_sigsegv_current(void);
        rust_send_sigsegv_current();
        return;
    }

    uint64_t prot = PAGE_USER_RW;
    if (vm_flags & VM_EXEC) {
        prot = PAGE_USER_RX;
    }

    phys_addr_t pa = alloc_phys_pages(0, GFP_KERNEL);
    if (!pa) {
        pf_puts("[PAGE FAULT] alloc failed\n");
        return;
    }

    void *va = phys_to_virt(pa);
    for (unsigned long i = 0; i < PAGE_SIZE; i++) {
        ((char *)va)[i] = 0;
    }

    int ret = map_page(page_addr, pa, prot);
    if (ret != 0) {
        free_phys_pages(pa, 0);
        pf_puts("[PAGE FAULT] map failed\n");
        return;
    }
}
