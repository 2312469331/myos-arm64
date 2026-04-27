#ifndef __TIMER_H
#define __TIMER_H

#include <stdint.h>

/* 根据硬件修改 */
#define TIMER_CLK_HZ 25000000UL
#define TICK_HZ 1000     // 1ms 一次调度 tick
#define TIMER_IRQ_NUM 30 // A53 CNTP 固定中断号

#define TIMER_LOAD_VAL (TIMER_CLK_HZ / TICK_HZ)

/* 全局系统时钟 tick */
extern volatile uint64_t system_tick;

/* 定时器回调函数类型 */
typedef void (*timer_callback_t)(void);

/* 定时器结构 */
struct timer {
    uint64_t expire_tick;      // 过期时间（系统 tick）
    timer_callback_t callback;  // 回调函数
    const char *name;           // 定时器名称
    struct timer *next;         // 下一个定时器
};

/* 初始化 CNTP 定时器（在 EL1 调用） */
void timer_init(void);

void cntp_set_tval(uint64_t tval);

/*
 * 添加定时器
 * @expire_ms: 过期时间（毫秒）
 * @callback: 回调函数
 * @name: 定时器名称
 * @return: 定时器指针，失败返回 NULL
 */
struct timer *timer_add(uint32_t expire_ms, timer_callback_t callback, const char *name);

/*
 * 删除定时器
 * @timer: 要删除的定时器
 * @return: 成功返回 0，失败返回 -1
 */
int timer_del(struct timer *timer);

/*
 * 获取当前系统 tick
 * @return: 当前系统 tick
 */
uint64_t timer_get_tick(void);

/*
 * 获取当前系统时间（毫秒）
 * @return: 当前系统时间（毫秒）
 */
uint64_t timer_get_time_ms(void);

#endif
