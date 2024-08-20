#include "uart.h"

// UART0 基地址（QEMU virt平台）
#define UART0_BASE 0x09000000

void uart_init(void) {
    // 这里可以添加UART初始化代码（可选）
}

void uart_putc(char c) {
    // 向UART发送一个字符
    volatile unsigned int *UART0_DR = (volatile unsigned int *)UART0_BASE;
    *UART0_DR = c;
}

void uart_puts(const char *s) {
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

