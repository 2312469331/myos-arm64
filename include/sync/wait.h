#ifndef _LINUX_WAIT_H
#define _LINUX_WAIT_H

#include <ds/list.h>
#include <sync/spinlock.h>


struct task_struct;

// 假设的基础等待队列结构，实际内核极复杂
struct wait_queue_entry {
    struct task_struct *task;
    struct list_head entry;
};
struct wait_queue_head {
    struct list_head head;
    spinlock_t lock;
};

void add_wait_queue(struct wait_queue_head *wq, struct wait_queue_entry *wq_entry);
void remove_wait_queue(struct wait_queue_head *wq, struct wait_queue_entry *wq_entry);
void wake_up(struct wait_queue_head *wq); // 唤醒第一个
void wake_up_all(struct wait_queue_head *wq); // 唤醒所有
extern void schedule(void); // 触发内核调度（让出CPU）

#endif
