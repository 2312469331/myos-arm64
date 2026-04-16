#ifndef _PAGE_FAULT_H
#define _PAGE_FAULT_H

#include <types.h>

/* =========================================================
 * 页错误异常处理
 * =========================================================
 */

void page_fault_handler(uint64_t fault_addr, uint64_t esr, uint64_t far);

/* =========================================================
 * 延迟映射支持
 * =========================================================
 */

int vmalloc_handle_page_fault(uint64_t fault_addr);

#endif /* _PAGE_FAULT_H */
