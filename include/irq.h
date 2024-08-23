#ifndef __IRQ_H__
#define __IRQ_H__

#include <stdint.h>
#include "exception.h"

// QEMU virt 外设中断号
#define IRQ_UART0    33  // 串口0中断
#define IRQ_TIMER0   27  // 核心定时器中断

// 中断回调函数类型
typedef void (*irq_handler_t)(uint32_t irq, exception_ctx_t *ctx);

// 中断注册函数
void irq_register(uint32_t irq, irq_handler_t handler, const char *name);

#endif // __IRQ_H__

