#include <timer.h>
#include <irq.h>
#include <printk.h>
#include <gic.h>
#include <sync/spinlock.h>
#include <slab.h>

volatile uint64_t system_tick = 0;
static uint64_t timer_load_val = 0;

typedef void (*timer_callback_t)(void);

static struct timer *timer_list = NULL;
static spinlock_t timer_lock = SPIN_LOCK_UNLOCKED;

static void cntp_enable(void) {
  uint64_t ctl = 0;
  ctl = (1 << 0) | (0 << 1);
  asm volatile("msr CNTP_CTL_EL0, %0" : : "r"(ctl));
}

void cntp_set_tval(uint64_t tval) {
  asm volatile("msr CNTP_TVAL_EL0, %0" : : "r"(tval));
}
extern void rust_timer_tick(void);
__attribute__((weak)) void timer_irq_handler(uint32_t irq) {
    (void)irq;
    rust_timer_tick();
    system_tick++;
    
    cntp_set_tval(timer_load_val);
    
    unsigned long flags;
    spin_lock_irqsave(&timer_lock, flags);
    
    struct timer **prev = &timer_list;
    struct timer *current = timer_list;
    
    while (current) {
        if (current->expire_tick <= system_tick) {
            struct timer *to_remove = current;
            *prev = current->next;
            current = *prev;
            
            spin_unlock_irqrestore(&timer_lock, flags);
            
            if (to_remove->callback) {
                to_remove->callback();
            }
            
            spin_lock_irqsave(&timer_lock, flags);
        } else {
            prev = &current->next;
            current = current->next;
        }
    }
    
    spin_unlock_irqrestore(&timer_lock, flags);
}

struct timer *timer_add(uint32_t expire_ms, timer_callback_t callback, const char *name) {
    if (!callback) {
        return NULL;
    }
    
    uint64_t expire_tick = system_tick + (expire_ms * TICK_HZ) / 1000;
    
    struct timer *new_timer = (struct timer *)kmalloc(sizeof(struct timer), GFP_KERNEL);
    if (!new_timer) {
        return NULL;
    }
    
    new_timer->expire_tick = expire_tick;
    new_timer->callback = callback;
    new_timer->name = name;
    new_timer->next = NULL;
    
    unsigned long flags;
    spin_lock_irqsave(&timer_lock, flags);
    
    if (!timer_list || expire_tick < timer_list->expire_tick) {
        new_timer->next = timer_list;
        timer_list = new_timer;
    } else {
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
            
            kfree(current);
            return 0;
        }
        prev = &current->next;
        current = current->next;
    }
    
    spin_unlock_irqrestore(&timer_lock, flags);
    return -1;
}

uint64_t timer_get_tick(void) {
    return system_tick;
}

uint64_t timer_get_time_ms(void) {
    return (system_tick * 1000) / TICK_HZ;
}

void timer_init(void) {
  uint64_t actual_freq;
  asm volatile("mrs %0, CNTFRQ_EL0" : "=r"(actual_freq));

  uint64_t cntpct;
  asm volatile("mrs %0, CNTPCT_EL0" : "=r"(cntpct));
  if (cntpct == 0) {
    printk("[TIMER] Warning: System counter not running\n");
  }

  uint64_t load_val = actual_freq / TICK_HZ;
  cntp_set_tval(load_val);
  timer_load_val = load_val;
  cntp_enable();

  irq_register(TIMER_IRQ_NUM, timer_irq_handler, "Timer");

//   uint64_t tval;
//   asm volatile("mrs %0, CNTP_TVAL_EL0" : "=r"(tval));
//   printk("[TIMER] CNTP_TVAL_EL0 = %llu\n", tval);
//   asm volatile("mrs %0, CNTP_TVAL_EL0" : "=r"(tval));
//   printk("[TIMER] CNTP_TVAL_EL0 = %llu\n", tval);
//     asm volatile("mrs %0, CNTP_TVAL_EL0" : "=r"(tval));
//   printk("[TIMER] CNTP_TVAL_EL0 = %llu\n", tval);
//     asm volatile("mrs %0, CNTP_TVAL_EL0" : "=r"(tval));
//   printk("[TIMER] CNTP_TVAL_EL0 = %llu\n", tval);
//     asm volatile("mrs %0, CNTP_TVAL_EL0" : "=r"(tval));
//   printk("[TIMER] CNTP_TVAL_EL0 = %llu\n", tval);
//     asm volatile("mrs %0, CNTP_TVAL_EL0" : "=r"(tval));
//   printk("[TIMER] CNTP_TVAL_EL0 = %llu\n", tval);


  printk("[TIMER] Initialized, tick rate: %d Hz, counter freq: %llu Hz, IRQ: %d\n", TICK_HZ, actual_freq, TIMER_IRQ_NUM);
}