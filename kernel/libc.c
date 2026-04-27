#include "libc.h"

// 原有实现
size_t strlen(const char *s) {
  size_t len = 0;
  while (s[len])
    len++;
  return len;
}

size_t strnlen(const char *s, size_t maxlen) {
  size_t len = 0;
  while (len < maxlen && s[len])
    len++;
  return len;
}

int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *pa = (const unsigned char *)a;
  const unsigned char *pb = (const unsigned char *)b;
  for (size_t i = 0; i < n; i++) {
    if (pa[i] != pb[i])
      return pa[i] - pb[i];
  }
  return 0;
}

void *memmove(void *dst, const void *src, size_t n) {
  unsigned char *pdst = (unsigned char *)dst;
  const unsigned char *psrc = (const unsigned char *)src;
  if (pdst < psrc) {
    for (size_t i = 0; i < n; i++)
      pdst[i] = psrc[i];
  } else {
    for (size_t i = n; i > 0; i--)
      pdst[i - 1] = psrc[i - 1];
  }
  return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
  unsigned char *pdst = (unsigned char *)dst;
  const unsigned char *psrc = (const unsigned char *)src;
  for (size_t i = 0; i < n; i++)
    pdst[i] = psrc[i];
  return dst;
}

void *memset(void *dst, int c, size_t n) {
  unsigned char *pdst = (unsigned char *)dst;
  for (size_t i = 0; i < n; i++)
    pdst[i] = (unsigned char)c;
  return dst;
}

// 新增：memchr 实现（内存中查找字符）
void *memchr(const void *s, int c, size_t n) {
  const unsigned char *p = (const unsigned char *)s;
  unsigned char ch = (unsigned char)c;
  for (size_t i = 0; i < n; i++) {
    if (p[i] == ch)
      return (void *)&p[i];
  }
  return NULL;
}

// 新增：strrchr 实现（字符串中反向查找字符）
char *strrchr(const char *s, int c) {
  char *last = NULL;
  while (*s) {
    if (*s == (char)c)
      last = (char *)s;
    s++;
  }
  if (c == '\0')
    last = (char *)s; // 处理查找'\0'的场景
  return last;
}

// 新增：strcmp 实现（字符串比较）
int strcmp(const char *s1, const char *s2) {
  while (*s1 && *s2 && *s1 == *s2) {
    s1++;
    s2++;
  }
  return (unsigned char)*s1 - (unsigned char)*s2;
}

// 新增：strstr 实现（查找子字符串）
char *strstr(const char *s1, const char *s2) {
  if (!*s2) return (char *)s1;
  
  while (*s1) {
    const char *p1 = s1;
    const char *p2 = s2;
    
    while (*p1 && *p2 && *p1 == *p2) {
      p1++;
      p2++;
    }
    
    if (!*p2) return (char *)s1;
    s1++;
  }
  
  return NULL;
}

// Rust FFI 兼容层
#include <printk.h>
#include <slab.h>

// 用于 Rust 侧打印字符串
void c_print_str(const char *s) {
    printk("%s", s);
}

// 用于 Rust 侧内存分配
void *rust_kmalloc(size_t size) {
    // return NULL;//测试给rust返回NULL默认行为用的
    return kmalloc(size, GFP_KERNEL);
}

// 用于 Rust 侧内存释放
void rust_kfree(void *ptr, size_t size) {
    kfree(ptr);
}

