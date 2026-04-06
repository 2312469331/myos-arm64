#ifndef __TYPES_H__
#define __TYPES_H__

// 裸机环境手动定义 NULL，替代标准库 <stddef.h>
#ifndef NULL
#define NULL ((void*)0)
#endif

// 避免与系统 stdint.h 重定义，仅在未定义时才声明
#ifndef uint8_t
typedef unsigned char       uint8_t;
#endif

#ifndef uint16_t
typedef unsigned short      uint16_t;
#endif

#ifndef uint32_t
typedef unsigned int        uint32_t;
#endif

// AArch64 下 uint64_t 标准类型为 unsigned long，而非 unsigned long long
#ifndef uint64_t
#ifdef __aarch64__
typedef unsigned long       uint64_t;
#else
typedef unsigned long long  uint64_t;
#endif
#endif

// 可选：定义有符号类型（保持一致性）
#ifndef int8_t
typedef signed char         int8_t;
#endif

#ifndef int16_t
typedef signed short        int16_t;
#endif

#ifndef int32_t
typedef signed int          int32_t;
#endif

#ifndef int64_t
#ifdef __aarch64__
typedef signed long         int64_t;
#else
typedef signed long long    int64_t;
#endif
#endif

// 定义 size_t 类型
#ifndef size_t
#ifdef __aarch64__
typedef unsigned long       size_t;
#else
typedef unsigned int        size_t;
#endif
#endif

#endif // __TYPES_H__

