#ifndef __SEMAPHORE_H__
#define __SEMAPHORE_H__

#include <types.h>
#include <sync/spinlock.h>

// 信号量类型
typedef struct semaphore {
    spinlock_t lock;
    int count;
} semaphore_t;

// 初始化信号量
static inline void semaphore_init(semaphore_t *sem, int count) {
    spin_lock_init(&sem->lock);
    sem->count = count;
}

// P操作（获取信号量）
void semaphore_down(semaphore_t *sem);

// V操作（释放信号量）
void semaphore_up(semaphore_t *sem);

// 尝试获取信号量
int semaphore_trydown(semaphore_t *sem);

// 获取信号量当前值
static inline int semaphore_get_value(semaphore_t *sem) {
    return sem->count;
}

#endif /* __SEMAPHORE_H__ */
