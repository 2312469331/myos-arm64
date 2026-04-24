#ifndef _LINUX_RCUPDATE_H
#define _LINUX_RCUPDATE_H

/*
 * 1. 抢占控制桩 (自写OS初期，先做成空操作，保证编译通过)
 * 真实的 rcu_read_lock 本质就是 preempt_disable()
 */
#define preempt_disable()  do { } while (0)
#define preempt_enable()   do { } while (0)

/* 读端接口：极简，无锁 */
#define rcu_read_lock()    preempt_disable()
#define rcu_read_unlock()  preempt_enable()

/*
 * 2. 回调结构体 (去掉未定义的 callback_head，自己定义一个轻量级的)
 * 注意：在 OS 支持异步回调前，这个结构体其实用不到。
 */
struct rcu_head {
    struct rcu_head *next;          // 用于挂链表
    void (*func)(struct rcu_head *head); // 回调函数
};
/* call_rcu 是异步版本，初期自写 OS 极度不建议实现，先注释掉避免诱惑 */
// void call_rcu(struct rcu_head *head, void (*func)(struct rcu_head *head));

/* 写端只声明这一个同步等待接口即可 */
void synchronize_rcu(void);
void call_rcu(struct rcu_head *head, void (*func)(struct rcu_head *head));

#endif
