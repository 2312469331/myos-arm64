#include "timer.h"
#include "irq.h"
#include "printk.h"
#include <sync/spinlock.h>
#include <slab.h>
volatile uint64_t system_tick = 0;

/* 定时器回调函数类型 */
typedef void (*timer_callback_t)(void);



/* 定时器链表 */
static struct timer *timer_list = NULL;
static spinlock_t timer_lock = SPIN_LOCK_UNLOCKED;

/*
 * 使能 CNTP 定时器
 * ENABLE=1, IMASK=0（开中断）
 */
static void cntp_enable(void) {
  uint64_t ctl = 0;
  ctl = (1 << 0) | (0 << 1);
  asm volatile("msr CNTP_CTL_EL0, %0" : : "r"(ctl));
}

/*
 * 设置倒计时重载值
 */
void cntp_set_tval(uint64_t tval) {
  asm volatile("msr CNTP_TVAL_EL0, %0" : : "r"(tval));
}

/*
 * 定时器中断处理函数
 */
__attribute__((weak)) void timer_irq_handler(uint32_t irq) {
    (void)irq;  // 消除未使用参数警告
    
    // 1. 递增系统 tick
    system_tick++;
    
    // 2. 重新加载定时器值
    cntp_set_tval(TIMER_LOAD_VAL);
    
    // 3. 处理定时器回调
    unsigned long flags;
    spin_lock_irqsave(&timer_lock, flags);
    
    struct timer **prev = &timer_list;
    struct timer *current = timer_list;
    
    while (current) {
        if (current->expire_tick <= system_tick) {
            // 定时器到期，调用回调
            struct timer *to_remove = current;
            *prev = current->next;
            current = *prev;
            
            // 解锁后调用回调，避免回调中再次获取锁导致死锁
            spin_unlock_irqrestore(&timer_lock, flags);
            
            if (to_remove->callback) {
                to_remove->callback();
            }
            
            // 重新加锁继续处理
            spin_lock_irqsave(&timer_lock, flags);
        } else {
            prev = &current->next;
            current = current->next;
        }
    }
    
    spin_unlock_irqrestore(&timer_lock, flags);
}

/*
 * 添加定时器
 * @expire_ms: 过期时间（毫秒）
 * @callback: 回调函数
 * @name: 定时器名称
 * @return: 定时器指针，失败返回 NULL
 */
struct timer *timer_add(uint32_t expire_ms, timer_callback_t callback, const char *name) {
    if (!callback) {
        return NULL;
    }
    
    // 计算过期 tick
    uint64_t expire_tick = system_tick + (expire_ms * TICK_HZ) / 1000;
    
    // 分配定时器结构（使用 kmalloc）
    struct timer *new_timer = (struct timer *)kmalloc(sizeof(struct timer), GFP_KERNEL);
    if (!new_timer) {
        return NULL;
    }
    
    // 初始化定时器
    new_timer->expire_tick = expire_tick;
    new_timer->callback = callback;
    new_timer->name = name;
    new_timer->next = NULL;
    
    // 添加到定时器链表
    unsigned long flags;
    spin_lock_irqsave(&timer_lock, flags);
    
    if (!timer_list || expire_tick < timer_list->expire_tick) {
        // 插入到链表头部
        new_timer->next = timer_list;
        timer_list = new_timer;
    } else {
        // 插入到合适位置
        struct timer *current = timer_list;
        while (current->next && current->next->expire_tick < expire_tick) {
            current = current->next;
        }
        new_timer->next = current->next;
        current->next = new_timer;
    }
    
    spin_unlock_irqrestore(&timer_lock, flags);
    
    return new_timer;
}

/*
 * 删除定时器
 * @timer: 要删除的定时器
 * @return: 成功返回 0，失败返回 -1
 */
int timer_del(struct timer *timer) {
    if (!timer) {
        return -1;
    }
    
    unsigned long flags;
    spin_lock_irqsave(&timer_lock, flags);
    
    struct timer **prev = &timer_list;
    struct timer *current = timer_list;
    
    while (current) {
        if (current == timer) {
            *prev = current->next;
            spin_unlock_irqrestore(&timer_lock, flags);
            
            // 释放定时器结构
            kfree(current);
            return 0;
        }
        prev = &current->next;
        current = current->next;
    }
    
    spin_unlock_irqrestore(&timer_lock, flags);
    return -1;
}

/*
 * 获取当前系统 tick
 * @return: 当前系统 tick
 */
uint64_t timer_get_tick(void) {
    return system_tick;
}

/*
 * 获取当前系统时间（毫秒）
 * @return: 当前系统时间（毫秒）
 */
uint64_t timer_get_time_ms(void) {
    return (system_tick * 1000) / TICK_HZ;
}

/*
 * 定时器初始化：只做 CNTP 本身
 * GIC 中断使能自己在外部调用 gic_enable_irq(TIMER_IRQ_NUM)
 */
void timer_init(void) {
  cntp_set_tval(TIMER_LOAD_VAL);
  cntp_enable();
  
  // 注册定时器中断处理函数
  irq_register(TIMER_IRQ_NUM, timer_irq_handler, "Timer");
  
  printk("[TIMER] Initialized, tick rate: %d Hz\n", TICK_HZ);
}
