#ifndef _LINUX_SEMAPHORE_H
#define _LINUX_SEMAPHORE_H

#include <ds/list.h>
#include <sync/wait.h>
#include <sync/spinlock.h>

struct semaphore {
    atomic_t count;         // 计数器
    struct wait_queue_head wait; // 等待队列
};

#define __SEMAPHORE_INITIALIZER(name, count) \
    { .count = ATOMIC_INIT(count), .wait = __WAIT_QUEUE_HEAD_INITIALIZER(name.wait) }

void down(struct semaphore *sem);
void up(struct semaphore *sem);

#endif
