#ifndef _LINUX_ATOMIC_H
#define _LINUX_ATOMIC_H

#include <asm/atomic.h>

// 通用层直接暴露架构层实现，保持头文件一致性
#define atomic_inc(v)   atomic_add(1, (v))
#define atomic_dec(v)   atomic_add(-1, (v))

#endif
