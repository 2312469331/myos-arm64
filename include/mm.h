#ifndef __MM_H__
#define __MM_H__

#include <mm_defs.h>

struct mm_struct *mm_create(void);
void mm_destroy(struct mm_struct *mm);

int vma_create(struct mm_struct *mm, uint64_t vm_start, uint64_t vm_end, unsigned long vm_flags);
struct vma_area *vma_find(struct mm_struct *mm, uint64_t addr);

// mmap 匿名映射：在进程地址空间中分配 len 字节的虚拟内存
// addr=0 则自动选择地址，flags 指定权限
// 返回映射的虚拟地址，失败返回 0
uint64_t do_mmap(struct mm_struct *mm, uint64_t addr, uint64_t len, unsigned long flags);

// munmap：解映射并释放虚拟内存
int do_munmap(struct mm_struct *mm, uint64_t addr, uint64_t len);

#endif /* __MM_H__ */
