#include "uart.h"

void main(void) {
    // 初始化UART
    uart_init();

    // 打印字符串
    uart_puts("Hello, ARM64 OS from Termux!\n");

    // 死循环
    while (1) {
        // 可以在这里添加你的内核逻辑
    }
}

