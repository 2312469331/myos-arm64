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

/* 初始化 CNTP 定时器（在 EL1 调用） */
void timer_init(void);

void cntp_set_tval(uint64_t tval);
#endif
