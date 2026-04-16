#include <sync/mutex.h>

// 获取互斥锁
void mutex_lock(mutex_t *mutex) {
    while (1) {
        spin_lock(&mutex->lock);
        if (!mutex->locked) {
            mutex->locked = 1;
            // 这里应该设置当前线程ID作为owner
            // 暂时使用-1作为占位
            mutex->owner = -1;
            spin_unlock(&mutex->lock);
            break;
        }
        spin_unlock(&mutex->lock);
        // 这里应该实现线程调度，暂时使用忙等待
    }
}

// 释放互斥锁
void mutex_unlock(mutex_t *mutex) {
    spin_lock(&mutex->lock);
    mutex->locked = 0;
    mutex->owner = -1;
    spin_unlock(&mutex->lock);
}

// 尝试获取互斥锁
int mutex_trylock(mutex_t *mutex) {
    spin_lock(&mutex->lock);
    if (!mutex->locked) {
        mutex->locked = 1;
        mutex->owner = -1;
        spin_unlock(&mutex->lock);
        return 1;
    }
    spin_unlock(&mutex->lock);
    return 0;
}
