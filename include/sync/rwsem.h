#ifndef _LINUX_RWSEM_H
#define _LINUX_RWSEM_H

#include <sync/atomic.h>
#include <sync/wait.h>

/* 
 * 状态机设计 (32位整数拆分)：
 * 高16位：写者等待数 / 活跃写者 (非0表示有写者或写者在等)
 * 低16位：读者活跃数 (0~65535)
 */

struct rw_semaphore {
    atomic_t count;
    spinlock_t lock; // 新增：用自旋锁来保护 count 的复合操作
    struct wait_queue_head wait_list;
};

void down_read(struct rw_semaphore *sem);
void up_read(struct rw_semaphore *sem);
void down_write(struct rw_semaphore *sem);
void up_write(struct rw_semaphore *sem);

#endif
