#include "uart.h"
#include "types.h"

// 内部函数：计算波特率寄存器值并配置
static void uart_set_baudrate(uint32_t baudrate) {
    volatile uint32_t *ibrd = (volatile uint32_t *)(UART0_BASE + UART_IBRD);
    volatile uint32_t *fbrd = (volatile uint32_t *)(UART0_BASE + UART_FBRD);

    // 公式：BAUDDIV = 时钟频率 / (16 * 波特率)
    uint32_t bauddiv = UART_CLOCK / (16 * baudrate);
    *ibrd = bauddiv / 64;    // 整数部分
    *fbrd = bauddiv % 64;    // 小数部分（0~63）
}

void uart_init(void) {
    volatile uint32_t *cr = (volatile uint32_t *)(UART0_BASE + UART_CR);
    volatile uint32_t *lcr_h = (volatile uint32_t *)(UART0_BASE + UART_LCR_H);

    // 1. 先关闭UART，避免配置过程中异常
    *cr &= ~UART_CR_UARTEN;

    // 2. 配置波特率（默认115200）
    uart_set_baudrate(DEFAULT_BAUDRATE);

    // 3. 配置数据格式：8位数据、1位停止、无校验、关闭FIFO（裸机简化）
    *lcr_h = UART_LCR_H_WLEN8;

    // 4. 清除接收错误标志
    uart_clear_error();

    // 5. 使能UART + 发送 + 接收
    *cr = UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE;
}

void uart_putc(char c) {
    volatile uint32_t *fr = (volatile uint32_t *)(UART0_BASE + UART_FR);
    volatile uint32_t *dr = (volatile uint32_t *)(UART0_BASE + UART_DR);

    // 处理换行：\n 自动补 \r，解决终端换行错位
    if (c == '\n') {
        while ((*fr & UART_FR_TXFE) == 0); // 等待发送FIFO空
        *dr = '\r';
    }

    // 阻塞等待发送FIFO空
    while ((*fr & UART_FR_TXFE) == 0);
    *dr = (uint32_t)c;
}

void uart_puts(const char *s) {
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

char uart_getc(uart_error_t *err) {
    volatile uint32_t *fr = (volatile uint32_t *)(UART0_BASE + UART_FR);
    volatile uint32_t *dr = (volatile uint32_t *)(UART0_BASE + UART_DR);
    volatile uint32_t *rsr_ecr = (volatile uint32_t *)(UART0_BASE + UART_RSR_ECR);

    // 阻塞等待接收FIFO非空
    while ((*fr & UART_FR_RXFE) != 0);

    // 读取错误码（若需要）
    if (err != NULL) {
        *err = (uart_error_t)(*rsr_ecr & 0x0F);
        uart_clear_error(); // 读取后立即清除错误
    }

    // 返回接收的字符
    return (char)(*dr & 0xFF);
}

bool uart_putc_nonblock(char c) {
    volatile uint32_t *fr = (volatile uint32_t *)(UART0_BASE + UART_FR);
    volatile uint32_t *dr = (volatile uint32_t *)(UART0_BASE + UART_DR);

    // 检查发送FIFO是否满
    if ((*fr & UART_FR_TXFF) != 0) {
        return false;
    }

    *dr = (uint32_t)c;
    return true;
}

bool uart_getc_nonblock(char *c, uart_error_t *err) {
    if (c == NULL) return false;

    volatile uint32_t *fr = (volatile uint32_t *)(UART0_BASE + UART_FR);
    volatile uint32_t *dr = (volatile uint32_t *)(UART0_BASE + UART_DR);
    volatile uint32_t *rsr_ecr = (volatile uint32_t *)(UART0_BASE + UART_RSR_ECR);

    // 检查接收FIFO是否空
    if ((*fr & UART_FR_RXFE) != 0) {
        return false;
    }

    // 读取错误码
    if (err != NULL) {
        *err = (uart_error_t)(*rsr_ecr & 0x0F);
        uart_clear_error();
    }

    *c = (char)(*dr & 0xFF);
    return true;
}

bool uart_tx_ready(void) {
    volatile uint32_t *fr = (volatile uint32_t *)(UART0_BASE + UART_FR);
    return (*fr & UART_FR_TXFF) == 0;
}

bool uart_rx_ready(void) {
    volatile uint32_t *fr = (volatile uint32_t *)(UART0_BASE + UART_FR);
    return (*fr & UART_FR_RXFE) == 0;
}

void uart_clear_error(void) {
    volatile uint32_t *rsr_ecr = (volatile uint32_t *)(UART0_BASE + UART_RSR_ECR);
    *rsr_ecr = 0x0F; // 写入任意值清除所有错误标志
}

void uart_set_loopback(bool enable) {
    volatile uint32_t *cr = (volatile uint32_t *)(UART0_BASE + UART_CR);
    if (enable) {
        *cr |= UART_CR_LBE;
    } else {
        *cr &= ~UART_CR_LBE;
    }
}

