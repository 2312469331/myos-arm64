#include <mm.h>
#include <mm_defs.h>
#include <printk.h>

// current_mm 在 page_fault.c 中定义
// 此文件提供进程内存管理的 syscall 辅助函数

// 获取当前进程的 mm_struct（供 C 侧调用）
struct mm_struct *sys_get_current_mm(void) {
    extern struct mm_struct *current_mm;
    return current_mm;
}

// 设置当前进程的 mm_struct（供调度器调用）
void sys_set_current_mm(struct mm_struct *mm) {
    extern struct mm_struct *current_mm;
    current_mm = mm;
}
