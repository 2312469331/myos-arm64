#include "printk.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "uart.h"
#include <sync/spinlock.h>

static spinlock_t printk_lock = SPIN_LOCK_UNLOCKED;

#define PF_SIGNED    (1 << 0)
#define PF_HEX_UPPER (1 << 1)
#define PF_ALT_FORM  (1 << 2)
#define PF_ZERO_PAD  (1 << 3)
#define PF_LEFT_JUST (1 << 4)

static void print_char(char c) {
    uart_putc(c);
}

static void print_str(const char *s) {
    while (*s != '\0') {
        uart_putc(*s++);
    }
}

static int num_digits(uint64_t num, int base) {
    if (num == 0) return 1;
    int digits = 0;
    while (num > 0) {
        num /= base;
        digits++;
    }
    return digits;
}

static void print_num(uint64_t num, int base, int width, int flags) {
    static const char hex_lower[] = "0123456789abcdef";
    static const char hex_upper[] = "0123456789ABCDEF";
    const char *digits = (flags & PF_HEX_UPPER) ? hex_upper : hex_lower;
    
    int is_neg = (flags & PF_SIGNED) && ((int64_t)num < 0);
    if (is_neg) num = -(int64_t)num;
    
    int ndigits = num_digits(num, base);
    int pad_len = width - ndigits;
    if (is_neg) pad_len--;
    
    if (flags & PF_ALT_FORM) {
        if (base == 16) {
            print_str((flags & PF_HEX_UPPER) ? "0X" : "0x");
            pad_len -= 2;
        } else if (base == 8) {
            print_char('0');
            pad_len--;
        } else if (base == 2) {
            print_str("0b");
            pad_len -= 2;
        }
    }
    
    if (!(flags & PF_LEFT_JUST) && !(flags & PF_ZERO_PAD) && pad_len > 0) {
        while (pad_len--) print_char(' ');
    }
    
    if (is_neg) print_char('-');
    
    if ((flags & PF_ZERO_PAD) && pad_len > 0) {
        while (pad_len--) print_char('0');
    }
    
    if (num == 0) {
        print_char('0');
    } else {
        char buf[64];
        int i = 0;
        while (num > 0) {
            buf[i++] = digits[num % base];
            num /= base;
        }
        while (i > 0) print_char(buf[--i]);
    }
    
    if ((flags & PF_LEFT_JUST) && pad_len > 0) {
        while (pad_len--) print_char(' ');
    }
}

static void vprintk(const char *fmt, va_list args) {
    unsigned long flags;
    spin_lock_irqsave(&printk_lock, flags);
    
    while (*fmt != '\0') {
        if (*fmt == '%') {
            fmt++;
            
            int flags = 0;
            while ((*fmt == '-') || (*fmt == '+') || (*fmt == '0') || (*fmt == '#')) {
                switch (*fmt) {
                    case '-': flags |= PF_LEFT_JUST; break;
                    case '+': flags |= PF_SIGNED; break;
                    case '0': flags |= PF_ZERO_PAD; break;
                    case '#': flags |= PF_ALT_FORM; break;
                }
                fmt++;
            }
            
            int width = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
            
            int is_long = 0;
            if (*fmt == 'l') {
                fmt++;
                if (*fmt == 'l') {
                    is_long = 2;
                    fmt++;
                } else {
                    is_long = 1;
                }
            }
            
            switch (*fmt) {
                case 's': {
                    const char *s = va_arg(args, const char*);
                    print_str(s ? s : "(null)");
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    print_char(c);
                    break;
                }
                case 'd':
                case 'i': {
                    uint64_t num;
                    if (is_long == 2) num = (uint64_t)va_arg(args, int64_t);
                    else if (is_long == 1) num = (uint64_t)va_arg(args, long);
                    else num = (uint64_t)va_arg(args, int);
                    flags |= PF_SIGNED;
                    print_num(num, 10, width, flags);
                    break;
                }
                case 'u': {
                    uint64_t num;
                    if (is_long == 2) num = va_arg(args, uint64_t);
                    else if (is_long == 1) num = va_arg(args, unsigned long);
                    else num = va_arg(args, unsigned int);
                    print_num(num, 10, width, flags);
                    break;
                }
                case 'x': {
                    uint64_t num;
                    if (is_long >= 1) num = va_arg(args, uint64_t);
                    else num = va_arg(args, unsigned int);
                    print_num(num, 16, width, flags);
                    break;
                }
                case 'X': {
                    uint64_t num;
                    if (is_long >= 1) num = va_arg(args, uint64_t);
                    else num = va_arg(args, unsigned int);
                    flags |= PF_HEX_UPPER;
                    print_num(num, 16, width, flags);
                    break;
                }
                case 'o': {
                    uint64_t num;
                    if (is_long >= 1) num = va_arg(args, uint64_t);
                    else num = va_arg(args, unsigned int);
                    print_num(num, 8, width, flags);
                    break;
                }
                case 'b': {
                    uint64_t num;
                    if (is_long >= 1) num = va_arg(args, uint64_t);
                    else num = va_arg(args, unsigned int);
                    print_num(num, 2, width, flags);
                    break;
                }
                case 'p': {
                    uint64_t addr = (uint64_t)va_arg(args, void*);
                    flags |= PF_ALT_FORM;
                    print_num(addr, 16, width ? width : 16, flags);
                    break;
                }
                case 'z': {
                    fmt++;
                    if (*fmt == 'u') {
                        print_num(va_arg(args, size_t), 10, width, flags);
                    } else if (*fmt == 'x') {
                        print_num(va_arg(args, size_t), 16, width, flags);
                    }
                    break;
                }
                case '%': {
                    print_char('%');
                    break;
                }
                default: {
                    print_char('%');
                    if (*fmt != '\0') print_char(*fmt);
                    break;
                }
            }
        } else {
            print_char(*fmt);
        }
        fmt++;
    }
    
    spin_unlock_irqrestore(&printk_lock, flags);
}

void printk(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintk(fmt, args);
    va_end(args);
}

void panic(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    uart_puts("\n[PANIC] ");
    vprintk(fmt, args);
    uart_puts("\nKernel halted.\n");
    va_end(args);
    while (1);
}