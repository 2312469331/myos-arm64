#ifndef __EXCEPTION_H__
#define __EXCEPTION_H__

#include <stdint.h>

// 异常上下文结构体（匹配vector.S现场保存）
typedef struct {
  uint64_t x0, x1, x2, x3, x4, x5, x6, x7;
  uint64_t x8, x9, x10, x11, x12, x13, x14, x15;
  uint64_t x16, x17, x18, x19, x20, x21, x22, x23;
  uint64_t x24, x25, x26, x27, x28, x29;
  uint64_t sp;   // 栈指针
  uint64_t elr;  // 异常返回地址（PC）
  uint64_t spsr; // 状态寄存器
} exception_ctx_t;

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
void el1_sync_handler(exception_ctx_t *ctx);
void el1_irq_handler(exception_ctx_t *ctx);

#endif // __EXCEPTION_H__
