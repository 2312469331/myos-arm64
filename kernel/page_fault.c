#include <page_fault.h>
#include <vmalloc.h>
#include <mmu.h>
#include <pgtbl.h>
#include <printk.h>

/* =========================================================
 * 页错误异常处理
 * =========================================================
 */

void page_fault_handler(uint64_t fault_addr, uint64_t esr, uint64_t far) {
    printk("page_fault: fault_addr=0x%llx, esr=0x%llx, far=0x%llx\n", 
           fault_addr, esr, far);
    
    if (vmalloc_handle_page_fault(fault_addr) == 0) {
        printk("page_fault: vmalloc lazy mapping handled\n");
        return;
    }
    
    printk("page_fault: unhandled page fault, halting\n");
    while (1) {
        asm volatile("wfi");
    }
}

/* =========================================================
 * 延迟映射支持
 * =========================================================
 */

int vmalloc_handle_page_fault(uint64_t fault_addr) {
    struct vmalloc_area *area;
    uint64_t page_addr;
    unsigned int page_index;
    int ret;
    
    if (fault_addr < VMALLOC_START || fault_addr > VMALLOC_END) {
        return -1;
    }
    
    spin_lock(get_vmalloc_lock());
    area = find_vmalloc_area_unlocked(fault_addr);
    if (!area) {
        spin_unlock(get_vmalloc_lock());
        return -1;
    }
    
    if (!(area->flags & VMALLOC_LAZY)) {
        spin_unlock(get_vmalloc_lock());
        return -1;
    }
    
    page_addr = fault_addr & PAGE_MASK;
    page_index = (page_addr - area->start) >> PAGE_SHIFT;
    
    if (page_index >= area->nr_pages) {
        spin_unlock(get_vmalloc_lock());
        return -1;
    }
    
    ret = arm64_map_one_page(page_addr, area->pages[page_index]);
    spin_unlock(get_vmalloc_lock());
    
    if (ret != 0) {
        printk("vmalloc_handle_page_fault: failed to map page at 0x%llx\n", 
               page_addr);
        return -1;
    }
    
    printk("vmalloc_handle_page_fault: mapped page at 0x%llx\n", page_addr);
    
    return 0;
}
