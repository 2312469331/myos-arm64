#include "uart.h"
#include "printk.h"

// OS 主函数
void main(void) {
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
    while (1);
}

