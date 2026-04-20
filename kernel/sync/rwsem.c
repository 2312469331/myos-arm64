#include <sync/rwsem.h>
// 在自写 OS 中，特别是还没有完善调度器和等待队列前，用自旋锁包裹 read/write 是最稳定、最不容易出死锁 Bug 的做法。等内核跑顺了，再去追求极致性能替换成选择二。
void down_read(struct rw_semaphore *sem) {
    spin_lock(&sem->lock);
    // 只要高16位（写者标志）不为0，就说明有写者在操作或等待，读者必须等
    while ((atomic_read(&sem->count) & 0xFFFF0000) != 0) {
        spin_unlock(&sem->lock);
        // TODO: 这里本应加入 wait_list 睡眠，简单起见先让出CPU自旋
        // schedule();
        spin_lock(&sem->lock);
    }
    // 高16位为0，安全，低16位读者计数+1
    atomic_set(&sem->count, atomic_read(&sem->count) + 1);
    spin_unlock(&sem->lock);
}

void up_read(struct rw_semaphore *sem) {
    spin_lock(&sem->lock);
    atomic_set(&sem->count, atomic_read(&sem->count) - 1);
    // 如果有写者在等，唤醒它
    if (atomic_read(&sem->count) < 0) {
        // wake_up(&sem->wait_list);
    }
    spin_unlock(&sem->lock);
}

void down_write(struct rw_semaphore *sem) {
    spin_lock(&sem->lock);
    // 高16位加 0x10000（表示占据写者位置），低16位期望为0（没有读者）
    int new_count = atomic_read(&sem->count) + 0x10000;
    while (new_count != 0x10000) { // 如果不等于 0x10000，说明低16位有读者
        atomic_set(&sem->count, new_count - 0x10000); // 还原高16位
        spin_unlock(&sem->lock);
        // TODO: 加入 wait_list 睡眠等待所有读者退出
        // schedule();
        spin_lock(&sem->lock);
        new_count = atomic_read(&sem->count) + 0x10000;
    }
    atomic_set(&sem->count, new_count); // 成功获取写锁
    spin_unlock(&sem->lock);
}

void up_write(struct rw_semaphore *sem) {
    spin_lock(&sem->lock);
    // 高16位减去 0x10000，释放写锁
    atomic_set(&sem->count, atomic_read(&sem->count) - 0x10000);
    // wake_up_all(&sem->wait_list); // 唤醒所有等待的读者或写者
    spin_unlock(&sem->lock);
}