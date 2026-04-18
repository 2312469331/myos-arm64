#ifndef __SPINLOCK_H__
#define __SPINLOCK_H__

#include <types.h>

typedef struct spinlock {
  volatile int locked;
} spinlock_t;

/*
 * 初始化自旋锁
 */
static inline void spin_lock_init(spinlock_t *lock) { lock->locked = 0; }

/*
 * 加锁：ACQUIRE 内存序 + 独占加载/存储 + WFE 低功耗自旋
 */
static inline void spin_lock(spinlock_t *lock) {
  int status;

  asm volatile("1: ldaxr   %w0, [%1]\n"          // 独占加载 + Acquire 内存序
               "       cbnz    %w0, 2f\n"        // 已上锁 → 进入等待
               "       stlxr   %w0, %w2, [%1]\n" // 独占存储 + Release 内存序
               "       cbz     %w0, 3f\n"        // stlxr 成功 → 加锁完成
               "2: wfe\n"                        // 低功耗等待事件
               "       b       1b\n"             // 重试
               "3:\n"
               : "=&r"(status) // 早期破坏：避免寄存器冲突（Clang 必需要）
               : "r"(&lock->locked), "r"(1)
               : "memory");
}

/*
 * 尝试加锁：成功返回 1，失败返回 0
 */
static inline int spin_trylock(spinlock_t *lock) {
  int status;

  asm volatile("ldaxr   %w0, [%1]\n"
               "cbnz    %w0, 1f\n"
               "stlxr   %w0, %w2, [%1]\n"
               "1:\n"
               : "=&r"(status)
               : "r"(&lock->locked), "r"(1)
               : "memory");

  return (status == 0);
}

/*
 * 解锁：Release 内存序 + SEV 唤醒其他 CPU
 */
static inline void spin_unlock(spinlock_t *lock) {
  // stlr 自带 Release 语义，保证之前的所有读写都先于解锁
  asm volatile("stlr    wzr, [%0]\n"
               "sev\n" // 唤醒执行 wfe 的核心
               :
               : "r"(&lock->locked)
               : "memory");
}

/*
 * 原子查询是否上锁：使用 ldar 保证强内存序
 */
static inline int spin_is_locked(spinlock_t *lock) {
  int val;
  asm volatile("ldar    %w0, [%1]\n"
               : "=r"(val)
               : "r"(&lock->locked)
               : "memory");
  return val != 0;
}

#endif
