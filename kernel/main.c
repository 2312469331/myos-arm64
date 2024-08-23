#include "exception.h"
#include "gic.h"
#include "irq.h"
#include "printk.h"
#include "uart.h"

// 👇 新增：函数声明（告诉编译器这些函数后面会定义）
void uart_test(void);
void gic_test(void);
// 如果 uart_irq_callback 已在其他文件实现，添加 extern 声明（根据实际参数修改）
// 👇 修正：参数列表必须和定义完全匹配
extern void uart_irq_callback(uint32_t irq, exception_ctx_t *ctx);
extern void vector_table; // 引用汇编向量表
//
void main(void) {
  uart_test();
  gic_test();
  while (1)
    ;
}
void gic_test(void) {

  uart_puts("OS Boot Success!\n");

  // 1. 设置异常向量表基址
  write_sysreg(vbar_el1, (uint64_t)&vector_table);
  uart_puts("[Init] Exception Vector Table Done\n");

  // 2. 初始化GIC中断控制器
  gic_init();
  uart_puts("[Init] GICv2 Done\n");

  // 3. 注册UART中断
  irq_register(IRQ_UART0, uart_irq_callback, "UART0");

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
