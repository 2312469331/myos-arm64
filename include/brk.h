#ifndef __BRK_H__
#define __BRK_H__

#include <mm_defs.h>

// 扩展/收缩堆，返回新 brk 地址（失败返回旧 brk）
uint64_t do_brk(struct mm_struct *mm, uint64_t new_brk);

#endif /* __BRK_H__ */
