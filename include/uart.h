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

// ==================== 寄存器偏移（PL011 手册 Table 3-1） ====================
// ... 你现有的定义保持不变 ...
#define UART_IFLS 0x034 // 中断FIFO阈值选择寄存器（新增）
#define UART_RIS 0x03C  // 原始中断状态寄存器（新增）
#define UART_MIS 0x040  // 屏蔽后中断状态寄存器（新增）
// ... 你现有的 UART_IMSC / UART_ICR 保持不变 ...
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
// ==================== IFLS 寄存器（中断FIFO阈值） ====================
// 接收FIFO阈值选择（位5:3）
#define UART_IFLS_RXIFLSEL_1_8 (0U << 3) // ≥1/8满触发接收中断
#define UART_IFLS_RXIFLSEL_1_4 (1U << 3) // ≥1/4满
#define UART_IFLS_RXIFLSEL_1_2 (2U << 3) // ≥1/2满（复位默认值，最常用）
#define UART_IFLS_RXIFLSEL_3_4 (3U << 3) // ≥3/4满
#define UART_IFLS_RXIFLSEL_7_8 (4U << 3) // ≥7/8满

// 发送FIFO阈值选择（位2:0）
#define UART_IFLS_TXIFLSEL_1_8 (0U << 0) // ≤1/8满触发发送中断
#define UART_IFLS_TXIFLSEL_1_4 (1U << 0) // ≤1/4满
#define UART_IFLS_TXIFLSEL_1_2 (2U << 0) // ≤1/2满（复位默认值）
#define UART_IFLS_TXIFLSEL_3_4 (3U << 0) // ≤3/4满
#define UART_IFLS_TXIFLSEL_7_8 (4U << 0) // ≤7/8满
// ==================== IMSC/RIS/MIS/ICR 寄存器（中断控制） ====================
// 中断掩码/状态位（IMSC/RIS/MIS/ICR 共用位位置）
#define UART_IMSC_OEIM (1U << 10) // 溢出错误中断掩码
#define UART_IMSC_BEIM (1U << 9)  // 断帧错误中断掩码
#define UART_IMSC_PEIM (1U << 8)  // 奇偶校验错误中断掩码
#define UART_IMSC_FEIM (1U << 7)  // 帧格式错误中断掩码
#define UART_IMSC_RTIM (1U << 6)  // 接收超时中断掩码
#define UART_IMSC_TXIM (1U << 5)  // 发送中断掩码
#define UART_IMSC_RXIM (1U << 4)  // 接收中断掩码（最常用）

// Modem 状态中断（普通串口开发基本不用，保持屏蔽即可）
#define UART_IMSC_DSRMIM (1U << 3) // nUARTDSR Modem 中断
#define UART_IMSC_DCDMIM (1U << 2) // nUARTDCD Modem 中断
#define UART_IMSC_CTSMIM (1U << 1) // nUARTCTS Modem 中断
#define UART_IMSC_RIMIM (1U << 0)  // nUARTRI Modem 中断

// ICR 专用：写1清除中断（复用IMSC位定义，更直观）
#define UART_ICR_RXIC UART_IMSC_RXIM // 清除接收中断
#define UART_ICR_TXIC UART_IMSC_TXIM // 清除发送中断
#define UART_ICR_RTIC UART_IMSC_RTIM // 清除接收超时中断
#define UART_ICR_FEIC UART_IMSC_FEIM // 清除帧错误中断
#define UART_ICR_PEIC UART_IMSC_PEIM // 清除奇偶校验错误中断
#define UART_ICR_BEIC UART_IMSC_BEIM // 清除断帧错误中断
#define UART_ICR_OEIC UART_IMSC_OEIM // 清除溢出错误中断

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
void uart_irq_init(void);
#endif // __UART_H__
