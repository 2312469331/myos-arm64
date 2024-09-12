#include "exception.h"
#include "gic.h"
#include "io.h" // 确保包含 io.h
#include "irq.h"
#include "printk.h"
#include "types.h"
#include "uart.h" // 你的串口打印函数

#include <stdint.h>

// ==============================
// AArch64 ESR_EL1 异常类型宏
// 来自 ARMv8-A 架构手册 严格定义
// ==============================
#define ESR_ELx_EC_SHIFT 26
#define ESR_ELx_EC_MASK (0x3FUL << ESR_ELx_EC_SHIFT)

// EC 异常类编号
#define ESR_EC_UNKNOWN 0x00
#define ESR_EC_UNDEF_INSTR 0x01 // 未定义指令
#define ESR_EC_SVC_AARCH64 0x15 // SVC 系统调用（AArch64）
#define ESR_EC_IABORT 0x20      // 指令中止（非法取指）
#define ESR_EC_IRQ 0x22         // 外部中断 IRQ
#define ESR_EC_FIQ 0x23         // 快速中断 FIQ
#define ESR_EC_DABORT 0x24      // 数据中止（空指针/缺页/越权）
#define ESR_EC_SERROR 0x2F      // 系统错误 SError（总线/硬件错）

// ==============================
// 读 AArch64 系统寄存器
// ==============================
static inline uint64_t read_esr_el1(void) {
  uint64_t v;
  asm volatile("mrs %0, esr_el1" : "=r"(v));
  return v;
}
static inline uint64_t read_elr_el1(void) {
  uint64_t v;
  asm volatile("mrs %0, elr_el1" : "=r"(v));
  return v;
}
static inline uint64_t read_far_el1(void) {
  uint64_t v;
  asm volatile("mrs %0, far_el1" : "=r"(v));
  return v;
}
static inline uint64_t read_spsr_el1(void) {
  uint64_t v;
  asm volatile("mrs %0, spsr_el1" : "=r"(v));
  return v;
}

// ==============================
// 完整异常入口 C 处理函数
// ==============================
void c_exception_handler(void) {
  uint64_t esr = read_esr_el1();   // 异常原因
  uint64_t elr = read_elr_el1();   // 触发异常的 PC
  uint64_t far = read_far_el1();   // 数据/指令中止的地址
  uint64_t spsr = read_spsr_el1(); // 进入异常前的 PSTATE
  uint64_t ec = (esr & ESR_ELx_EC_MASK) >> ESR_ELx_EC_SHIFT;
  // 用 printk 打印所有关键信息
  printk("[Exception Handler] ELR (Fault PC): 0x%lx\n", elr);
  printk("[Exception Handler] FAR (Fault Address): 0x%lx\n", far);
  printk("[Exception Handler] ESR (Exception Syndrome): 0x%lx\n", esr);
  printk("[Exception Handler] ESR EC (Exception Class): 0x%lx\n", ec);
  printk("[Exception Handler] SPSR (PSTATE): 0x%lx\n", spsr);

  switch (ec) {
  // ======================
  // 1. IRQ 中断：直接跳去你的处理函数
  // ======================
  case ESR_EC_IRQ:
    el1_irq_handler();
    break;

  // ======================
  // 2. FIQ（快速中断，很少用）
  // ======================
  case ESR_EC_FIQ:
    // 你可以自己实现 el1_fiq_handler()
    while (1)
      ;
    break;

  // ======================
  // 3. 未定义指令
  // ======================
  case ESR_EC_UNDEF_INSTR:
    // 例：串口打印 "Undefined instruction at PC: 0x..."
    while (1)
      ;
    break;

  // ======================
  // 4. SVC 系统调用（AArch64）
  // ======================
  case ESR_EC_SVC_AARCH64:
    // 解析系统调用号、参数
    while (1)
      ;
    break;

  // ======================
  // 5. 指令取指异常（PC 指向非法内存）
  // ======================
  case ESR_EC_IABORT:
    // 错误地址 = elr（就是出错的 PC）
    while (1)
      ;
    break;

  // ======================
  // 6. 数据访问异常（空指针、缺页、越权访问）
  // ======================
  case ESR_EC_DABORT:
    // 错误地址 = far
    while (1)
      ;
    break;

  // ======================
  // 7. SError 硬件/总线错误
  // ======================
  case ESR_EC_SERROR:
    while (1)
      ;
    break;

  // ======================
  // 其他未知异常
  // ======================
  default:
    while (1)
      ;
    break;
  }
}

static irq_handler_t irq_table[1024] = {NULL};

// 注册中断回调
void irq_register(uint32_t irq, irq_handler_t handler, const char *name) {
  if (irq < 1024 && handler) {
    irq_table[irq] = handler;
    gic_enable_irq(irq);
    uart_puts("[IRQ] Register: ");
    uart_puts(name);
    uart_puts("\n");
  }
}

// EL1同步异常处理（核心：处理对齐错误）
void el1_sync_handler() {
  uint64_t esr = read_sysreg(esr_el1);
  uint64_t far = read_sysreg(far_el1);
  uint32_t ec = (esr >> 26) & 0x3F; // 异常类别

  // 替换为 printk 版本
  printk("\n[EL1 Sync Exception]\n");
  printk("ESR: 0x%x\n", esr);
  printk("FAR: 0x%x\n", far);

  // 未定义指令
  if (ec == 0x00) {
    uart_puts("[Error] Undefined Instruction!\n");
    while (1)
      ;
  }

  // 其他异常：死循环
  while (1)
    ;
}

// EL1 IRQ中断处理
void el1_irq_handler() {
  uint32_t iar = io_read32((volatile void *)GICC_IAR);
  uint32_t irq_num = iar & 0x3FF; // 提取中断号

  // 调用注册的回调函数
  if (irq_num < 1024 && irq_table[irq_num]) {
    irq_table[irq_num](irq_num);
  }

  io_write32((volatile void *)GICC_EOIR, iar);
}

// 规范的UART中断回调示例（以UART0为例）
// 注意：中断号要和实际硬件匹配（QEMU virt平台 UART0中断号通常是33）
__attribute__((weak)) void uart_irq_callback(uint32_t irq) {
  printk("%s", "sbgxr????");
  // 1. 标记参数（消除警告）
  (void)irq;
  // 改造后（使用 GIC 上层 API，更规范）
  uint32_t irq_num = gic_ack_irq(); // 应答中断
  // ... 处理 UART 数据 ...
  gic_eoi_irq(irq_num); // 结束中断

  // 3. 检查UART接收FIFO是否有数据（非阻塞）
  if (!uart_rx_ready()) {
    return; // 无数据直接返回，不浪费中断时间
  }
  // 4. 非阻塞读+错误处理（规范）
  uart_error_t err;
  char ch;
  uart_getc_nonblock(&ch, &err); // 现在 &ch 是 char*，和函数参数匹配
  if (err != UART_ERR_NONE) {
    // p
    uart_clear_error(); // 清除错误标志，避免卡死
    return;
  }
  // 5. 回显数据（仅做业务处理，快速退出）
  // 修正后（正确）
  uart_getc_nonblock(&ch, &err); // 现在 &ch 是 char*，和函数参数匹配
  if (err != UART_ERR_NONE) {
    // p
    uart_clear_error(); // 清除错误标志，避免卡死
    return;
  }
}
/*
 * 定时器中断入口
 * 你只需要在中断判定为 30 号时跳来这里
 */
// ---------------- 关键修改 ----------------
// 1. 加 __attribute__((weak)) 声明为弱符号
// 2. 函数体为空（默认不做任何事）
// 3. 签名必须匹配 irq_handler_t
__attribute__((weak)) void timer_irq_handler(uint32_t irq) {
  // 默认空实现，会被 main 里的强符号覆盖
  (void)irq; // 消除未使用参数警告
}
