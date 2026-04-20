#include <sync/wait.h>
/*
 * 等待队列的桩实现
 * 在你的调度器实现之前，这些函数什么实质工作都不做，仅仅是为了让同步锁模块能编译链接通过。
 * TODO: 等你写好了 task_struct 和 schedule()，再回来重写这里！
 */

void add_wait_queue(struct wait_queue_head *wq, struct wait_queue_entry *wq_entry) {
    // 暂时空实现
}

void remove_wait_queue(struct wait_queue_head *wq, struct wait_queue_entry *wq_entry) {
    // 暂时空实现
}

void wake_up(struct wait_queue_head *wq) {
    // 暂时空实现：等有了调度器，这里会从链表里拿出 task_struct，把它扔进就绪队列
}

void wake_up_all(struct wait_queue_head *wq) {
    // 暂时空实现
}