#ifndef __UART_H__
#define __UART_H__

#include <stdbool.h>
#include <stdint.h>

// ========================= 平台配置（QEMU virt ARM） =========================
#define UART0_BASE ((volatile void *)0x09000000)
#define UART_CLOCK 24000000U     // UART 时钟频率：24MHz
#define DEFAULT_BAUDRATE 115200U // 默认波特率

// ========================= 寄存器偏移（PL011 手册 Table 3-1）
// =========================
#define UART_DR 0x000      // 数据寄存器
#define UART_RSR_ECR 0x004 // 接收状态/错误清除寄存器
#define UART_FR 0x018      // 标志寄存器
#define UART_IBRD 0x024    // 整数波特率寄存器
#define UART_FBRD 0x028    // 小数波特率寄存器
#define UART_LCR_H 0x02C   // 线路控制寄存器（高）
#define UART_CR 0x030      // 控制寄存器
#define UART_IMSC 0x038    // 中断屏蔽设置/清除寄存器
#define UART_ICR 0x044     // 中断清除寄存器

// ========================= 寄存器关键位定义 =========================
// FR 寄存器
#define UART_FR_TXFE (1U << 7) // 发送FIFO空
#define UART_FR_RXFF (1U << 6) // 接收FIFO满
#define UART_FR_TXFF (1U << 5) // 发送FIFO满
#define UART_FR_RXFE (1U << 4) // 接收FIFO空
#define UART_FR_BUSY (1U << 3) // UART忙

// CR 寄存器
#define UART_CR_UARTEN (1U << 0) // UART使能
#define UART_CR_TXE (1U << 8)    // 发送使能
#define UART_CR_RXE (1U << 9)    // 接收使能
#define UART_CR_LBE (1U << 18)   // 回环模式使能

// LCR_H 寄存器
#define UART_LCR_H_WLEN8 (3U << 5) // 8位数据位
#define UART_LCR_H_FEN (1U << 4)   // FIFO使能（默认关闭，裸机简化用）

// 错误类型（RSR_ECR）
typedef enum {
  UART_ERR_NONE = 0x00,
  UART_ERR_OVERRUN = 0x01, // 溢出错误
  UART_ERR_BREAK = 0x02,   // 中断错误
  UART_ERR_PARITY = 0x04,  // 奇偶校验错误
  UART_ERR_FRAME = 0x08    // 帧错误
} uart_error_t;

// ========================= 核心 API 声明 =========================
/**
 * @brief 初始化 UART（默认115200波特率、8N1、无FIFO）
 */
void uart_init(void);

/**
 * @brief 阻塞式发送单个字符
 * @param c 要发送的字符（自动处理\n -> \r\n）
 */
void uart_putc(char c);

/**
 * @brief 阻塞式发送字符串
 * @param s 以'\0'结尾的字符串
 */
void uart_puts(const char *s);

/**
 * @brief 阻塞式接收单个字符
 * @param err 输出参数：接收过程中的错误码（传NULL则忽略）
 * @return 接收到的字符
 */
char uart_getc(uart_error_t *err);

/**
 * @brief 非阻塞式发送单个字符
 * @param c 要发送的字符
 * @return 成功返回true，失败（发送FIFO满）返回false
 */
bool uart_putc_nonblock(char c);

/**
 * @brief 非阻塞式接收单个字符
 * @param c 输出参数：接收到的字符
 * @param err 输出参数：错误码（传NULL则忽略）
 * @return 成功返回true，失败（接收FIFO空）返回false
 */
bool uart_getc_nonblock(char *c, uart_error_t *err);

/**
 * @brief 检查发送是否就绪（发送FIFO非满）
 * @return 就绪返回true，否则false
 */
bool uart_tx_ready(void);

/**
 * @brief 检查是否有接收数据（接收FIFO非空）
 * @return 有数据返回true，否则false
 */
bool uart_rx_ready(void);

/**
 * @brief 清除接收错误标志
 */
void uart_clear_error(void);

/**
 * @brief 使能UART回环模式（硬件调试用）
 * @param enable true-使能，false-禁用
 */
void uart_set_loopback(bool enable);

#endif // __UART_H__
