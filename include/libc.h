#ifndef __LIBC_H__
#define __LIBC_H__

#include <types.h>

// ============================================================
// libc 标准函数（使用编译器内置函数优化）
// ============================================================
// 策略：优先使用 __builtin_* 函数，编译器会生成高度优化的汇编代码
//       这些函数在 ARM64 上会使用 SIMD/NEON 指令加速
// ============================================================

// ------------------------------------------------------------
// 字符串操作
// ------------------------------------------------------------
size_t strlen(const char *s);           // 字符串长度
size_t strnlen(const char *s, size_t maxlen); // 字符串长度（带限制）
int strcmp(const char *s1, const char *s2);   // 字符串比较
char *strchr(const char *s, int c);    // 正向查找字符
char *strrchr(const char *s, int c);   // 反向查找字符
char *strcpy(char *dst, const char *src);     // 字符串复制
char *strncpy(char *dst, const char *src, size_t n); // 字符串复制（带限制）

// ------------------------------------------------------------
// 内存操作
// ------------------------------------------------------------
void *memcpy(void *dst, const void *src, size_t n);   // 内存复制（不处理重叠）
void *memmove(void *dst, const void *src, size_t n);  // 内存移动（处理重叠）
void *memset(void *dst, int c, size_t n);             // 内存设置
int memcmp(const void *a, const void *b, size_t n);   // 内存比较
void *memchr(const void *s, int c, size_t n);         // 内存查找

#endif // __LIBC_H__