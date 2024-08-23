#include "exception.h"
#include "gic.h"
#include "io.h" // 确保包含 io.h
#include "irq.h"
#include "printk.h"
#include "types.h"
#include "uart.h" // 你的串口打印函数

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
void el1_sync_handler(exception_ctx_t *ctx) {
  uint64_t esr = read_sysreg(esr_el1);
  uint64_t far = read_sysreg(far_el1);
  uint32_t ec = (esr >> 26) & 0x3F; // 异常类别

  // 替换为 printk 版本
  printk("\n[EL1 Sync Exception]\n");
  printk("ESR: 0x%x\n", esr);
  printk("FAR: 0x%x\n", far);
  printk("PC:  0x%x\n", ctx->elr);

  // 处理数据对齐错误（你之前的ldr未对齐问题）
  if (ec == 0x21) {
    uart_puts("[Error] Data Alignment Fault!\n");
    ctx->elr += 4; // 跳过错误指令（4字节）
    return;
  }

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
void el1_irq_handler(exception_ctx_t *ctx) {
  uint32_t iar = io_read32((volatile void *)GICC_IAR);
  uint32_t irq_num = iar & 0x3FF; // 提取中断号

  // 调用注册的回调函数
  if (irq_num < 1024 && irq_table[irq_num]) {
    irq_table[irq_num](irq_num, ctx);
  }

  io_write32((volatile void *)GICC_EOIR, iar);
}

// 规范的UART中断回调示例（以UART0为例）
// 注意：中断号要和实际硬件匹配（QEMU virt平台 UART0中断号通常是33）
void uart_irq_callback(uint32_t irq, exception_ctx_t *ctx) {
  // 1. 标记参数（消除警告）
  (void)irq;
  (void)ctx;
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
