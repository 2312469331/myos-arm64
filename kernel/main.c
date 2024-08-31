#include "exception.h"
#include "gic.h"
#include "irq.h"
#include "printk.h"
#include "timer.h"
#include "uart.h"
#include <stdint.h>

#include <stdint.h>

// 👇 新增：函数声明（告诉编译器这些函数后面会定义）
void uart_test(void);
void gic_test(void);
// uart_irq_callback 已在handler.c文件实现，添加 extern 声明（根据实际参数修改）
extern void uart_irq_callback(uint32_t irq, exception_ctx_t *ctx);
extern void timer_irq_handler(uint32_t irq, exception_ctx_t *ctx);
void main(void) {
  uart_test();
  gic_test();
  irq_register(TIMER_IRQ_NUM, timer_irq_handler, "定时器");
  gic_enable_irq(TIMER_IRQ_NUM);
  while (1)
    ;
}
void gic_test(void) {

  uart_puts("OS Boot Success!\n");

  // 1. 设置异常向量表基址,boot.S已经加载完向量表，无需加载
  // write_sysreg(vbar_el1, (uint64_t)&vector_table_el1);
  uart_puts("[Init] Exception Vector Table Done\n");

  // 2. 初始化GIC中断控制器
  gic_init();
  uart_puts("[Init] GICv2 Done\n");

  // 3. 注册UART中断
  irq_register(IRQ_UART0, uart_irq_callback, "UART0");
  gic_enable_irq(IRQ_UART0);
  // 4. 启用全局中断
  enable_irq();
  uart_puts("[Init] Global IRQ Enabled\n");

  // 测试：故意触发对齐异常（验证异常处理）
  // volatile uint64_t *p = (uint64_t *)0x40000fac;
  // uint64_t val = *p;
}

// OS 主函数
void uart_test(void) {
  uart_error_t err;
  char c;

  // 1. 初始化UART
  uart_init();

  // 2. 发送测试字符串
  uart_puts("=== PL011 UART Driver Test (QEMU virt ARM) ===\n");
  uart_puts("Input a character (echo mode): ");

  // 3. 回显模式：接收一个字符并发送回去
  c = uart_getc(&err);
  if (err != UART_ERR_NONE) {
    uart_puts("\nReceive error: ");
    uart_putc('0' + err);
  } else {
    uart_puts("\nYou input: ");
    uart_putc(c);
  }

  uart_puts("\nDriver test done!\n");
  printk("=== MyOS ARM64 Boot Successful ===\n");
  printk("UART initialized\n");
  printk("Test: %s, %d, %x, %c\n", "Hello Kernel", 1234, 0x42, 'X');

  // 测试 panic（可选，注释掉先验证 printk）
  // panic("Test panic: %s", "Something went wrong!");
  // 死循环（OS 无退出）
}
// 强符号：覆盖 timer.c 里的弱符号
void timer_irq_handler(uint32_t irq, exception_ctx_t *ctx) {
  // 1. 标记参数（消除警告）
  (void)irq;
  (void)ctx;
  // 1. 重载定时器，保证持续 tick（必须写，否则中断只触发一次）
  cntp_set_tval(TIMER_LOAD_VAL);
  // 2. 系统时间++
  system_tick++;
}
void uart_irq_callback(uint32_t irq, exception_ctx_t *ctx) {
  printk("%s", "sbgxr????");
  // 1. 标记参数（消除警告）
  (void)irq;
  (void)ctx;
  // el1_irq_handler(exception_ctx_t *ctx)自动应答不需要多此一举
  // // 改造后（使用 GIC 上层 API，更规范）
  // uint32_t irq_num = gic_ack_irq(); // 应答中断
  // // ... 处理 UART 数据 ...
  // gic_eoi_irq(irq_num); // 结束中断

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
