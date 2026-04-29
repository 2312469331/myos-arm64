#ifndef __EXCEPTION_H__
#define __EXCEPTION_H__

#include <stdint.h>

// 系统寄存器读写宏
#define write_sysreg(reg, val) asm volatile("msr " #reg ", %0" : : "r"(val))

#define read_sysreg(reg)                                                       \
  ({                                                                           \
    uint64_t val;                                                              \
    asm volatile("mrs %0, " #reg : "=r"(val));                                 \
    val;                                                                       \
  })

// 全局中断使能/禁用
#define enable_irq() asm volatile("msr daifclr, #2")
#define disable_irq() asm volatile("msr daifset, #2")

// 异常处理函数声明
void el1_sync_handler();
void el1_irq_handler();
void c_exception_handler_el1(uint64_t);
void c_exception_handler_el2(void);
void c_exception_handler_el3(void);

#endif // __EXCEPTION_H__
