#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__

#include <types.h>

// 自旋锁类型
typedef struct spinlock {
    int locked;
} spinlock_t;

// 初始化自旋锁
static inline void spin_lock_init(spinlock_t *lock) {
    lock->locked = 0;
}

// 获取自旋锁
static inline void spin_lock(spinlock_t *lock) {
    while (1) {
        // 使用内联汇编实现原子测试和设置
        int old;
        __asm__ volatile (
            "ldxr %w0, [%1]\n"
            "cbnz %w0, 1f\n"
            "stxr %w0, %w2, [%1]\n"
            "1:\n"
            : "=r" (old)
            : "r" (&lock->locked), "r" (1)
            : "memory"
        );
        if (!old) {
            break;
        }
        // 忙等待
    }
}

// 释放自旋锁
static inline void spin_unlock(spinlock_t *lock) {
    // 使用内联汇编实现原子清除
    __asm__ volatile (
        "stlr wzr, [%0]\n"
        :
        : "r" (&lock->locked)
        : "memory"
    );
}

// 尝试获取自旋锁
static inline int spin_trylock(spinlock_t *lock) {
    int old;
    __asm__ volatile (
        "ldxr %w0, [%1]\n"
        "cbnz %w0, 1f\n"
        "stxr %w0, %w2, [%1]\n"
        "1:\n"
        : "=r" (old)
        : "r" (&lock->locked), "r" (1)
        : "memory"
    );
    return !old;
}

// 检查自旋锁是否被持有
static inline int spin_is_locked(spinlock_t *lock) {
    return lock->locked;
}

#endif /* __SPINLOCK_H__ */
