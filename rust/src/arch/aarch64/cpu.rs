/// 架构相关 CPU 控制：中断开关与抢占计数。
/// 无外部依赖，仅使用 core 内联汇编。

use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

/// 全局抢占计数：>0 表示禁止调度。
pub static PREEMPT_COUNT: AtomicUsize = AtomicUsize::new(0);

/// 是否需要重新调度标志。
pub static NEED_RESCHED: AtomicBool = AtomicBool::new(false);

/// 禁用 IRQ（中断屏蔽），写入 DAIF 第 2 位。
#[inline]
pub fn disable_irq() {
    unsafe {
        core::arch::asm!("msr daifset, #2");
    }
}

/// 开启 IRQ。
#[inline]
pub fn enable_irq() {
    unsafe {
        core::arch::asm!("msr daifclr, #2");
    }
}

/// 禁用 IRQ 并返回之前的 DAIF 状态（用于 spin_lock_irqsave）。
#[inline]
pub fn disable_irq_save() -> u64 {
    let daif: u64;
    unsafe {
        core::arch::asm!("mrs {}, daif", out(reg) daif);
        core::arch::asm!("msr daifset, #2");
    }
    daif
}

/// 从保存的 DAIF 状态恢复（用于 spin_unlock_irqrestore）。
#[inline]
pub fn restore_irq(flags: u64) {
    unsafe {
        core::arch::asm!("msr daif, {}", in(reg) flags);
    }
}

/// 读取当前 CPU 的 MPIDR_EL1（Aff0 域），支持 8 核以内。
#[inline]
pub fn cpu_id() -> usize {
    let mpidr: u64;
    unsafe { core::arch::asm!("mrs {}, mpidr_el1", out(reg) mpidr); }
    (mpidr & 0xff) as usize
}

/// 增加抢占计数（禁止抢占）。
#[inline]
pub fn preempt_disable() {
    PREEMPT_COUNT.fetch_add(1, Ordering::Relaxed);
}

/// 减少抢占计数，若归零则允许抢占。
#[inline]
pub fn preempt_enable() {
    PREEMPT_COUNT.fetch_sub(1, Ordering::Relaxed);
}