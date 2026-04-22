#ifndef __GFP_H
#define __GFP_H

/*
 * GFP 内存分配标志 (Get Free Page flags)
 */
typedef unsigned int gfp_t;

/* 核心分配掩码 */
#define GFP_KERNEL    0x001  /* 常规内核分配，可以睡眠 */
#define GFP_ATOMIC    0x002  /* 原子分配，不能睡眠（中断/自旋锁） */
#define GFP_DMA       0x004  /* 从 DMA 区域分配 */
#define GFP_HIGHMEM   0x008  /* 高端内存 */

#endif /* __GFP_H */