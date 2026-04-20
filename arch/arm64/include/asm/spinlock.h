#ifndef _ASM_ARM64_SPINLOCK_H
#define _ASM_ARM64_SPINLOCK_H

#include <types.h>

/* 
 * Ticket Lock (票据锁) - ARM64 默认的自旋锁实现
 * 解决 FIFO 公平性问题
 */
typedef struct {
    unsigned int next;   // 下一个发放的票据
    unsigned int owner;  // 当前服务的票据
} arch_spinlock_t;

static inline void arch_spin_lock(arch_spinlock_t *lock) {
    unsigned int tmp, ticket;
    asm volatile(
    "1: ldaxr %w0, [%3]\n"       // 独占获取 next
    "   add %w1, %w0, #1\n"
    "   stxr %w2, %w1, [%3]\n"   // 尝试更新 next
    "   cbnz %w2, 1b\n"          // 更新失败重试
    /* 拿到票据，开始等待服务 */
    "2: wfe\n"                   // 等待事件，降低功耗
    "   ldaxr %w2, [%4]\n"       // 加载 owner
    "   cmp %w0, %w2\n"
    "   b.ne 2b\n"               // 如果不等，继续睡
    : "=&r" (ticket), "=&r" (tmp), "=&r" (tmp)
    : "r" (&lock->next), "r" (&lock->owner)
    : "memory");
}

static inline void arch_spin_unlock(arch_spinlock_t *lock) {
    asm volatile(
    "   stlr %w1, [%0]\n"        // 释放语义存储 owner+1，隐式唤醒 WFE
    :: "r" (&lock->owner), "r" (lock->owner + 1)
    : "memory");
}

#endif
