#ifndef _LINUX_COMPILER_H
#define _LINUX_COMPILER_H

#include <asm/complier.h>

/*
 * 精简版 READ_ONCE / WRITE_ONCE 实现
 * 核心原理：利用 C 语言的 volatile 关键字，剥夺编译器对该变量的优化权。
 * typeof 是 GCC 的扩展，用于自动推导类型。
 */

#define READ_ONCE(x)	(*(volatile typeof(x) *)&(x))
#define WRITE_ONCE(x, val)	(*(volatile typeof(x) *)&(x) = (val))

// /*
//  * 内核/驱动 标准内存屏障宏
//  * ARM 官方内置指令屏障（__isb / __dsb / __dmb）
//  */
// #define __ISB()         __isb(0xF)
// #define __DSB()         __dsb(0xF)
// #define __DMB()         __dmb(0xF)

/*
 * Linux 内核风格屏障宏（最常用！）
 */
// 全能内存屏障（写操作必备：DSB + ISB）
#define mb()            do { __DSB(); __ISB(); } while (0)
// 读内存屏障
#define rmb()           __DMB()
// 写内存屏障
#define wmb()           __DSB()

// 设备 IO 屏障（驱动最常用）
#define dma_wmb()       __DSB()
#define dma_rmb()       __DMB()

// 编译器屏障（禁止指令重排优化，不产生CPU指令）
#define barrier()       __asm__ __volatile__("" ::: "memory")

#endif /* _LINUX_COMPILER_H */