#ifndef __PRINTK_H__
#define __PRINTK_H__

#include "types.h"
#include "uart.h"

// 格式化打印，支持 %s、%d、%x、%c
void printk(const char *fmt, ...);

// 致命错误处理：打印信息后死循环
void panic(const char *fmt, ...);

#endif // __PRINTK_H__

