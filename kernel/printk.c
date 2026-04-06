#include "printk.h"
#include <stdarg.h>
#include "uart.h"

// 辅助：打印字符串
static void print_str(const char *s) {
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

// 辅助：打印十进制整数（支持int）
static void print_dec(int num) {
    if (num == 0) {
        uart_putc('0');
        return;
    }
    char buf[20];
    int i = 0;
    bool is_neg = false;

    if (num < 0) {
        is_neg = true;
        num = -num;
    }
    while (num > 0) {
        buf[i++] = (num % 10) + '0';
        num /= 10;
    }
    if (is_neg) {
        uart_putc('-');
    }
    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

// 辅助：打印十六进制（通用，支持32/64位）
static void print_hex(uint64_t num) {
//    char hex_chars[] = "0123456789abcdef";
    static const char *hex_chars = "0123456789abcdef";

    char buf[16];
    int i = 0;

    if (num == 0) {
        uart_puts("0x0");
        return;
    }
    while (num > 0) {
        buf[i++] = hex_chars[num % 16];
        num /= 16;
    }
    uart_puts("0x");
    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

// 核心 printk 实现
void printk(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    while (*fmt != '\0') {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 's': // 字符串
                    print_str(va_arg(args, const char*));
                    break;
                case 'd': // 十进制int
                    print_dec(va_arg(args, int));
                    break;
                case 'x': { // 32位十六进制（修复AArch64栈布局）
                    // AArch64下，32位参数会被零扩展到64位，按64位读取后截断
                    uint64_t num = va_arg(args, uint64_t);
                    print_hex(num & 0xFFFFFFFF); // 只取低32位
                    break;
                }
                case 'l': // 64位十六进制 %lx
                    fmt++;
                    if (*fmt == 'x') {
                        print_hex(va_arg(args, uint64_t));
                    } else {
                        uart_putc('%');
                        uart_putc('l');
                        if (*fmt != '\0') {
                            uart_putc(*fmt);
                        }
                    }
                    break;
                case 'c': // 字符
                    uart_putc((char)va_arg(args, int)); // char提升为int
                    break;
                case 'p': // 指针地址（64位）
                    print_hex((uint64_t)va_arg(args, void*));
                    break;
                default: // 未知格式化符
                    uart_putc('%');
                    uart_putc(*fmt);
                    break;
            }
        } else {
            uart_putc(*fmt);
        }
        fmt++;
    }

    va_end(args);
}

// 致命错误 panic
void panic(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    uart_puts("\n[PANIC] ");
    while (*fmt != '\0') {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 's':
                    print_str(va_arg(args, const char*));
                    break;
                case 'd':
                    print_dec(va_arg(args, int));
                    break;
                case 'x': {
                    uint64_t num = va_arg(args, uint64_t);
                    print_hex(num & 0xFFFFFFFF);
                    break;
                }
                case 'l':
                    fmt++;
                    if (*fmt == 'x') {
                        print_hex(va_arg(args, uint64_t));
                    }
                    break;
                case 'c':
                    uart_putc((char)va_arg(args, int));
                    break;
                case 'p': // 指针地址（64位）
                    print_hex((uint64_t)va_arg(args, void*));
                    break;
                default:
                    uart_putc('%');
                    uart_putc(*fmt);
                    break;
            }
        } else {
            uart_putc(*fmt);
        }
        fmt++;
    }

    va_end(args);
    uart_puts("\nKernel halted.\n");
    while (1); // 死循环，停止内核
}

