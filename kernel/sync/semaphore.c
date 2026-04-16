#include <sync/semaphore.h>

// P操作（获取信号量）
void semaphore_down(semaphore_t *sem) {
    while (1) {
        spin_lock(&sem->lock);
        if (sem->count > 0) {
            sem->count--;
            spin_unlock(&sem->lock);
            break;
        }
        spin_unlock(&sem->lock);
        // 这里应该实现线程调度，暂时使用忙等待
    }
}

// V操作（释放信号量）
void semaphore_up(semaphore_t *sem) {
    spin_lock(&sem->lock);
    sem->count++;
    spin_unlock(&sem->lock);
}

// 尝试获取信号量
int semaphore_trydown(semaphore_t *sem) {
    spin_lock(&sem->lock);
    if (sem->count > 0) {
        sem->count--;
        spin_unlock(&sem->lock);
        return 1;
    }
    spin_unlock(&sem->lock);
    return 0;
}
