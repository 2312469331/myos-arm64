#ifndef _ASM_ARM64_IRQFLAGS_H
#define _ASM_ARM64_IRQFLAGS_H

/* 
 * Cortex-A53 通过 DAIF (Debug, Asynchronous, IRQ, FIQ) 寄存器控制中断 
 * 使用内联汇编直接操作系统寄存器
 */
static inline unsigned long arch_local_irq_save(void) {
    unsigned long flags;
    asm volatile("mrs %0, daif\n"
                 "msr daifset, #2\n" // #2 对应 IRQ bit
                 : "=r" (flags) :: "memory");
    return flags;
}

static inline void arch_local_irq_enable(void) {
    // 注意这里变成了三个冒号 :::
    asm volatile("msr daifclr, #2\n" ::: "memory");
}

static inline void arch_local_irq_disable(void) {
    // 注意这里变成了三个冒号 :::
    asm volatile("msr daifset, #2\n" ::: "memory");
}


static inline void arch_local_irq_restore(unsigned long flags) {
    asm volatile("msr daif, %0\n" :: "r" (flags) : "memory");
}

#endif
