#include "uart.h"
#include "io.h" // 新增：引入封装好的IO API
#include "types.h"

// 全局UART基地址，在main函数中动态计算并设置
volatile void *uart_base = NULL;

// 辅助宏：计算UART寄存器地址
#define UART_REG(reg) ((volatile void *)((uint64_t)(UART0_BASE) + (uint64_t)(reg)))

// 内部函数：计算波特率寄存器值并配置
static void uart_set_baudrate(uint32_t baudrate) {
  // 公式：BAUDDIV = 时钟频率 / (16 * 波特率)
  uint32_t bauddiv = UART_CLOCK / (16 * baudrate);
  // 整数部分
  io_write32(UART_REG(UART_IBRD), bauddiv / 64);
  // 小数部分 (0~63)
  io_write32(UART_REG(UART_FBRD), bauddiv % 64);
}

void uart_init(void) {
  // 1. 先关闭UART，避免配置过程中异常
  uint32_t cr_val = io_read32(UART_REG(UART_CR));
  io_write32(UART_REG(UART_CR), cr_val & ~UART_CR_UARTEN);

  // 2. 配置波特率 (默认115200)
  uart_set_baudrate(DEFAULT_BAUDRATE);

  // 3. 配置数据格式：8位数据、1位停止、无校验、关闭FIFO (裸机简化)
  io_write32(UART_REG(UART_LCR_H), UART_LCR_H_WLEN8);

  // 4. 清除接收错误标志
  uart_clear_error();
  // 配置接收中断
  uart_irq_init();
  // 5. 使能UART + 发送 + 接收
  io_write32(UART_REG(UART_CR), UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE);
}

void uart_putc(char c) {
  // 处理换行：\n 自动补 \r，解决终端换行错位
  if (c == '\n') {
    while ((io_read32(UART_REG(UART_FR)) & UART_FR_TXFE) == 0)
      ; // 等待发送FIFO空
    io_write32(UART_REG(UART_DR), '\r');
  }

  // 阻塞等待发送FIFO空
  while ((io_read32(UART_REG(UART_FR)) & UART_FR_TXFE) == 0)
    ;
  io_write32(UART_REG(UART_DR), (uint32_t)c);
}

void uart_puts(const char *s) {
  while (*s != '\0') {
    uart_putc(*s++);
  }
}

char uart_getc(uart_error_t *err) {
  // 阻塞等待接收FIFO非空
  while ((io_read32(UART_REG(UART_FR)) & UART_FR_RXFE) != 0)
    ;

  // 读取错误码 (若需要)
  if (err != NULL) {
    uint32_t rsr_val = io_read32(UART_REG(UART_RSR_ECR));
    *err = (uart_error_t)(rsr_val & 0x0F);
    uart_clear_error(); // 读取后立即清除错误
  }

  // 返回接收的字符
  uint32_t dr_val = io_read32(UART_REG(UART_DR));
  return (char)(dr_val & 0xFF);
}

bool uart_putc_nonblock(char c) {
  // 检查发送FIFO是否满
  if ((io_read32(UART_REG(UART_FR)) & UART_FR_TXFF) != 0) {
    return false;
  }

  io_write32(UART_REG(UART_DR), (uint32_t)c);
  return true;
}

bool uart_getc_nonblock(char *c, uart_error_t *err) {
  if (c == NULL)
    return false;

  // 检查接收FIFO是否空
  if ((io_read32(UART_REG(UART_FR)) & UART_FR_RXFE) != 0) {
    return false;
  }

  // 读取错误码
  if (err != NULL) {
    uint32_t rsr_val = io_read32(UART_REG(UART_RSR_ECR));
    *err = (uart_error_t)(rsr_val & 0x0F);
    uart_clear_error();
  }

  uint32_t dr_val = io_read32(UART_REG(UART_DR));
  *c = (char)(dr_val & 0xFF);
  return true;
}

bool uart_tx_ready(void) {
  return (io_read32(UART_REG(UART_FR)) & UART_FR_TXFF) == 0;
}

bool uart_rx_ready(void) {
  return (io_read32(UART_REG(UART_FR)) & UART_FR_RXFE) == 0;
}

void uart_clear_error(void) {
  // 写入任意值清除所有错误标志
  io_write32(UART_REG(UART_RSR_ECR), 0x0F);
}

void uart_set_loopback(bool enable) {
  uint32_t cr_val = io_read32(UART_REG(UART_CR));
  if (enable) {
    cr_val |= UART_CR_LBE;
  } else {
    cr_val &= ~UART_CR_LBE;
  }

  io_write32(UART_REG(UART_CR), cr_val);
}
// 配置 UART 接收中断（半满触发）
void uart_irq_init(void) {
  // 设置接收FIFO半满触发
  io_write32(UART_REG(UART_IFLS),
             UART_IFLS_RXIFLSEL_1_2 | UART_IFLS_TXIFLSEL_1_2);

  // 使能接收中断
  io_write32(UART_REG(UART_IMSC), UART_IMSC_RXIM);

  // 清除接收中断标志
  io_write32(UART_REG(UART_ICR), UART_ICR_RXIC);
}
