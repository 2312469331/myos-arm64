#ifndef _ASM_ARM64_ATOMIC_H
#define _ASM_ARM64_ATOMIC_H

#include <types.h>
#include <compiler.h>
/* 
 * Cortex-A53 独占指令对 (LL/SC 机制)
 * 使用带获取/释放语义的 LDAXR/STXR 隐式包含内存屏障
 */
static inline void atomic_add(int i, atomic_t *v) {
    int val;
    asm volatile(
    "1: ldaxr %w0, [%2]\n"   // 独占加载
    "   add %w0, %w0, %w1\n"
    "   stxr %w0, %w0, [%2]\n"// 独占存储，失败时 %w0=1
    "   cbnz %w0, 1b\n"      // 重试
    : "=&r" (val)
    : "r" (i), "r" (&v->counter)
    : "memory");
}

static inline void atomic_inc(atomic_t *v) { atomic_add(1, v); }
static inline void atomic_dec(atomic_t *v) { atomic_add(-1, v); }

static inline int atomic_read(const atomic_t *v) {
    return *(volatile int *)&v->counter;
}

static inline void atomic_set(atomic_t *v, int i) {
    WRITE_ONCE(v->counter, i);
}

#endif
