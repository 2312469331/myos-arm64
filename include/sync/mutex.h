#ifndef __MUTEX_H__
#define __MUTEX_H__

#include <types.h>
#include <sync/spinlock.h>

// 互斥锁类型
typedef struct mutex {
    spinlock_t lock;
    int locked;
    int owner;
} mutex_t;

// 初始化互斥锁
static inline void mutex_init(mutex_t *mutex) {
    spin_lock_init(&mutex->lock);
    mutex->locked = 0;
    mutex->owner = -1;
}

// 获取互斥锁
void mutex_lock(mutex_t *mutex);

// 释放互斥锁
void mutex_unlock(mutex_t *mutex);

// 尝试获取互斥锁
int mutex_trylock(mutex_t *mutex);

// 检查互斥锁是否被持有
static inline int mutex_is_locked(mutex_t *mutex) {
    return mutex->locked;
}

#endif /* __MUTEX_H__ */
