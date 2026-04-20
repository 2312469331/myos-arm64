#ifndef _LINUX_COMPLETION_H
#define _LINUX_COMPLETION_H

#include <sync/wait.h>

struct completion {
    unsigned int done;
    struct wait_queue_head wait;
};

#define COMPLETION_INITIALIZER(name) \
    { .done = 0, .wait = __WAIT_QUEUE_HEAD_INITIALIZER(name.wait) }
static inline void init_waitqueue_head(struct wait_queue_head *wq_head)
{
    // 将链表头初始化为指向自己，表示空链表 (Linux内核经典的双向循环链表写法)
    wq_head->head.next = &wq_head->head;
    wq_head->head.prev = &wq_head->head;
}
static inline void init_completion(struct completion *c) {
    c->done = 0;
    init_waitqueue_head(&c->wait);
}

void wait_for_completion(struct completion *c);
void complete(struct completion *c);

#endif
