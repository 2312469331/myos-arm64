#include <sync/completion.h>
#include <compiler.h>
void wait_for_completion(struct completion *c) {
    // 原子读，如果没完成，就去睡
    if (!READ_ONCE(c->done)) {
        struct wait_queue_entry wq_entry;
        // ... 加入 c->wait 队列 ...
        // ... 调用 schedule() 睡眠 ...
        // ... 被唤醒后跳出 ...
    }
    WRITE_ONCE(c->done, 0); // 消耗掉一次完成信号
}

void complete(struct completion *c) {
    WRITE_ONCE(c->done, 1);
    wake_up(&c->wait); // 唤醒等待的线程
}
