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
// 2. pt_regs 结构体（顺序必须和你汇编 stp 的顺序严格对应！
// 第一个必须是 elr 和 spsr，因为你是在存完它们之后才 bl 到 C 的
struct pt_regs {
    uint64_t elr_el1;
    uint64_t spsr_el1;
    uint64_t padding;  // xzr
    uint64_t x30;
    uint64_t x29;
    uint64_t x28;
    uint64_t x27;
    uint64_t x26;
    uint64_t x25;
    uint64_t x24;
    uint64_t x23;
    uint64_t x22;
    uint64_t x21;
    uint64_t x20;
    uint64_t x19;
    uint64_t x18;
    uint64_t x17;
    uint64_t x16;
    uint64_t x15;
    uint64_t x14;
    uint64_t x13;
    uint64_t x12;
    uint64_t x11;
    uint64_t x10;
    uint64_t x9;
    uint64_t x8;
    uint64_t x7;
    uint64_t x6;
    uint64_t x5;
    uint64_t x4;
    uint64_t x3;
    uint64_t x2;
    uint64_t x1;
    uint64_t x0;
};
// 异常处理函数声明
void el1_sync_handler();
void el1_irq_handler();
void c_exception_handler_el1(uint64_t ex_type, struct pt_regs *regs);
void c_exception_handler_el2(void);
void c_exception_handler_el3(void);

#endif // __EXCEPTION_H__
