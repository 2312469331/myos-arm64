#include "timer.h"

volatile uint64_t system_tick = 0;

/*
 * 使能 CNTP 定时器
 * ENABLE=1, IMASK=0（开中断）
 */
static void cntp_enable(void) {
  uint64_t ctl = 0;
  ctl = (1 << 0) | (0 << 1);
  asm volatile("msr CNTP_CTL_EL0, %0" : : "r"(ctl));
}

/*
 * 设置倒计时重载值
 */
void cntp_set_tval(uint64_t tval) {
  asm volatile("msr CNTP_TVAL_EL0, %0" : : "r"(tval));
}

/*
 * 定时器初始化：只做 CNTP 本身
 * GIC 中断使能你自己在外部调用你的 gic_enable_irq(TIMER_IRQ_NUM)
 */
void timer_init(void) {
  cntp_set_tval(TIMER_LOAD_VAL);
  cntp_enable();
}
