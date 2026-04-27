#ifndef __IRQ_H__
#define __IRQ_H__

#include <stdint.h>

// QEMU virt 外设中断号
#define IRQ_UART0 33  // 串口0中断
#define IRQ_TIMER0 29 // EL1 物理定时器中断 (PPI 16+13=29)

// 中断回调函数类型
typedef void (*irq_handler_t)(uint32_t irq);

// 中断注册函数
void irq_register(uint32_t irq, irq_handler_t handler, const char *name);

#endif // __IRQ_H__
