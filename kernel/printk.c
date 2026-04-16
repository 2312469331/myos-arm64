#include "printk.h"
#include <stdarg.h>
#include "uart.h"

// 辅助：打印字符串
static void print_str(const char *s) {
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

// 辅助：打印十进制整数（支持int和unsigned int）
static void print_dec(unsigned int num) {
    if (num == 0) {
        uart_putc('0');
        return;
    }
    char buf[20];
    int i = 0;

    while (num > 0) {
        buf[i++] = (num % 10) + '0';
        num /= 10;
    }
    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

// 辅助：打印有符号十进制整数
static void print_dec_signed(int num) {
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

// 辅助：打印64位无符号整数
static void print_dec_64(uint64_t num) {
    if (num == 0) {
        uart_putc('0');
        return;
    }
    char buf[20];
    int i = 0;

    while (num > 0) {
        buf[i++] = (num % 10) + '0';
        num /= 10;
    }
    while (i > 0) {
        uart_putc(buf[--i]);
    }
}

// 辅助：打印十六进制（支持指定宽度）
static void print_hex(uint64_t num, int width) {
    static const char *hex_chars = "0123456789abcdef";

    char buf[16];
    int i = 0;

    uart_puts("0x");
    
    if (num == 0) {
        uart_putc('0');
        return;
    }
    
    while (num > 0) {
        buf[i++] = hex_chars[num % 16];
        num /= 16;
    }
    
    // 补零
    for (int j = i; j < width; j++) {
        uart_putc('0');
    }
    
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
            
            // 解析宽度
            int width = 0;
            if (*fmt == '0') {
                // 零填充，暂时忽略，直接跳过
                fmt++;
            }
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
            
            switch (*fmt) {
                case 's': // 字符串
                    print_str(va_arg(args, const char*));
                    break;
                case 'd': // 十进制int
                    print_dec_signed(va_arg(args, int));
                    break;
                case 'u': // 无符号十进制int
                    print_dec(va_arg(args, unsigned int));
                    break;
                case 'x': { // 32位十六进制（修复AArch64栈布局）
                    // AArch64下，32位参数会被零扩展到64位，按64位读取后截断
                    uint64_t num = va_arg(args, uint64_t);
                    print_hex(num & 0xFFFFFFFF, width); // 只取低32位
                    break;
                }
                case 'l': // 64位
                    fmt++;
                    if (*fmt == 'x') {
                        print_hex(va_arg(args, uint64_t), width);
                    } else if (*fmt == 'l') {
                        fmt++;
                        if (*fmt == 'u') {
                            // %llu 64位无符号整数
                            uint64_t num = va_arg(args, uint64_t);
                            print_dec_64(num);
                        }
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
                    print_hex((uint64_t)va_arg(args, void*), 16);
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
            
            // 解析宽度
            int width = 0;
            if (*fmt == '0') {
                // 零填充，暂时忽略，直接跳过
                fmt++;
            }
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
            
            switch (*fmt) {
                case 's':
                    print_str(va_arg(args, const char*));
                    break;
                case 'd':
                    print_dec_signed(va_arg(args, int));
                    break;
                case 'u':
                    print_dec(va_arg(args, unsigned int));
                    break;
                case 'x': {
                    uint64_t num = va_arg(args, uint64_t);
                    print_hex(num & 0xFFFFFFFF, width);
                    break;
                }
                case 'l':
                    fmt++;
                    if (*fmt == 'x') {
                        print_hex(va_arg(args, uint64_t), width);
                    } else if (*fmt == 'l') {
                        fmt++;
                        if (*fmt == 'u') {
                            // %llu 64位无符号整数
                            uint64_t num = va_arg(args, uint64_t);
                            print_dec_64(num);
                        }
                    }
                    break;
                case 'c':
                    uart_putc((char)va_arg(args, int));
                    break;
                case 'p': // 指针地址（64位）
                    print_hex((uint64_t)va_arg(args, void*), 16);
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

