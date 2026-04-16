#ifndef __LIBC_H__
#define __LIBC_H__

#include <types.h> // 确保size_t正确定义

// 字符串/内存操作函数（严格遵循ISO C标准）
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t maxlen);
int memcmp(const void *a, const void *b, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
// 新增：memchr/strrchr 标准声明
void *memchr(const void *s, int c, size_t n);
char *strrchr(const char *s, int c);

#endif

