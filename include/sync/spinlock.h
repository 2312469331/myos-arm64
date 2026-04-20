#ifndef _LINUX_SPINLOCK_H
#define _LINUX_SPINLOCK_H

#include <asm/spinlock.h>
#include <asm/irqflags.h>

typedef struct {
    arch_spinlock_t raw_lock;
    unsigned int magic;
} spinlock_t;


/* 静态编译期初始化 */
#define SPIN_LOCK_UNLOCKED   { .raw_lock = { .next = 0, .owner = 0 }, .magic = 0xDEADBEEF }

/* 运行期动态初始化 (补上这个) */
#define spin_lock_init(lock)                   \
    do {                                        \
        (lock)->raw_lock.next = 0;              \
        (lock)->raw_lock.owner = 0;             \
        (lock)->magic = 0xDEADBEEF;            \
    } while (0)

static inline void spin_lock(spinlock_t *lock) {
    arch_spin_lock(&lock->raw_lock);
}

static inline void spin_unlock(spinlock_t *lock) {
    arch_spin_unlock(&lock->raw_lock);
}

/* 结合中断控制的自旋锁 (最常用的安全组合) */
static inline unsigned long spin_lock_irqsave(spinlock_t *lock) {
    unsigned long flags = arch_local_irq_save();
    arch_spin_lock(&lock->raw_lock);
    return flags;
}

static inline void spin_unlock_irqrestore(spinlock_t *lock, unsigned long flags) {
    arch_spin_unlock(&lock->raw_lock);
    arch_local_irq_restore(flags);
}

/* 9. 本地中断禁止的直接封装 */
#define local_irq_disable() arch_local_irq_disable()
#define local_irq_enable()  arch_local_irq_enable()
#define local_irq_save(flags) (flags = arch_local_irq_save())
#define local_irq_restore(flags) arch_local_irq_restore(flags)

#endif
