#ifndef _LINUX_MUTEX_H
#define _LINUX_MUTEX_H

#include <sync/semaphore.h>

/* 本质上是封装了信号量，但限制 count=1，并增加 owner 调试信息 */
struct mutex {
    atomic_t count;       // 1:空闲, 0:被占用, 负数:有等待者
    spinlock_t wait_lock; // 保护等待队列的自旋锁
    struct wait_queue_head wait_list;
    void *owner;          // 记录谁拿了锁，用于死锁检测
};

#define MUTEX_INITIALIZER(name) \
    { .count = ATOMIC_INIT(1), .wait_lock = SPIN_LOCK_UNLOCKED, \
      .wait_list = __WAIT_QUEUE_HEAD_INITIALIZER(name.wait_list), .owner = NULL }

void mutex_lock(struct mutex *mtx);
void mutex_unlock(struct mutex *mtx);

#endif
