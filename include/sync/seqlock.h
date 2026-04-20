#ifndef _LINUX_SEQLOCK_H
#define _LINUX_SEQLOCK_H

#include <sync/spinlock.h>
#include <sync/atomic.h>
#include <compiler.h>

typedef struct {
    unsigned int sequence;
    spinlock_t lock;
} seqlock_t;

#define SEQLOCK_UNLOCKED { .sequence = 0, .lock = SPIN_LOCK_UNLOCKED }

/* 写者：加自旋锁，序列号+1(变奇数表示正在写) */
static inline void write_seqlock(seqlock_t *sl) {
    spin_lock(&sl->lock);
    sl->sequence++;
    __DMB(); // 写屏障，确保数据写在序列号变回偶数之前
}

static inline void write_sequnlock(seqlock_t *sl) {
    __DMB();
    sl->sequence++; // 变偶数，表示写完
    spin_unlock(&sl->lock);
}

/* 读者：无锁，发现序列号变了就重试 */
static inline unsigned int read_seqbegin(const seqlock_t *sl) {
    unsigned int seq = sl->sequence;
    __DMB(); // 读屏障
    return seq;
}

static inline int read_seqretry(const seqlock_t *sl, unsigned int seq) {
    __DMB();
    // 如果序列号变了，或者现在是奇数（正在写），说明读到脏数据
    return seq != sl->sequence;
}

#endif
