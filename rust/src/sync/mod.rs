/// 内核同步原语
///
/// - `KMutex` — Ticket Lock 自旋锁 + irqsave（多核安全、FIFO 公平）
/// - `Semaphore` — 计数信号量（可睡眠阻塞）
/// - `Mutex` — 互斥锁（基于信号量，可睡眠阻塞）

use core::cell::UnsafeCell;
use core::ops::{Deref, DerefMut};
use core::sync::atomic::{AtomicU32, Ordering};
use crate::arch::aarch64::cpu;

pub mod semaphore;
pub mod mutex;

pub use semaphore::Semaphore;
pub use mutex::Mutex;

// ============================================================
//  Ticket Lock — 与 C 侧 arch_spinlock_t 相同的算法
// ============================================================

/// Ticket Lock：保证 FIFO 公平性，多核安全。
///
/// 算法：每个 acquirer 原子递增 `next` 拿到票据，
/// 等待 `owner` 追上自己的票据号即获锁。
/// 释放时递增 `owner`，隐式唤醒下一个等待者。
pub struct TicketLock {
    next: AtomicU32,
    owner: AtomicU32,
}

unsafe impl Sync for TicketLock {}
unsafe impl Send for TicketLock {}

impl TicketLock {
    pub const fn new() -> Self {
        TicketLock {
            next: AtomicU32::new(0),
            owner: AtomicU32::new(0),
        }
    }

    /// 获取锁：原子拿票 → WFE 等待 → 获锁。
    #[inline]
    pub fn lock(&self) {
        let ticket = self.next.fetch_add(1, Ordering::Relaxed);
        // 等待 owner 追上我的票据
        while self.owner.load(Ordering::Acquire) != ticket {
            unsafe { core::arch::asm!("wfe"); }
        }
    }

    /// 释放锁：递增 owner，隐式 SEV 唤醒下一个 WFE 等待者。
    #[inline]
    pub fn unlock(&self) {
        self.owner.fetch_add(1, Ordering::Release);
    }

    /// 尝试获取锁（非阻塞）。
    #[inline]
    pub fn try_lock(&self) -> bool {
        let next = self.next.load(Ordering::Relaxed);
        let owner = self.owner.load(Ordering::Acquire);
        if next == owner {
            // 尝试原子拿票
            self.next.compare_exchange(
                next,
                next + 1,
                Ordering::Acquire,
                Ordering::Relaxed,
            ).is_ok()
        } else {
            false
        }
    }
}

// ============================================================
//  KMutex — Ticket Lock + irqsave（替代原有关中断方案）
// ============================================================

/// 内核互斥锁：Ticket Lock 保证公平 + 关中断防止本地抢占。
///
/// - 单核：关中断足以互斥，Ticket Lock 是多余的但无害
/// - 多核：Ticket Lock 保证跨核互斥，关中断防止死锁
pub struct KMutex<T> {
    data: UnsafeCell<T>,
    lock: TicketLock,
}

// 内核中 KMutex 由 TicketLock + 关中断保证互斥，声明为 Sync / Send 安全。
unsafe impl<T: Send> Sync for KMutex<T> {}
unsafe impl<T: Send> Send for KMutex<T> {}

impl<T> KMutex<T> {
    /// `const fn` 构造函数，可在静态初始化中使用。
    pub const fn new(val: T) -> Self {
        KMutex {
            data: UnsafeCell::new(val),
            lock: TicketLock::new(),
        }
    }

    /// 获取锁：关中断 + 获取 Ticket Lock，返回守卫。
    pub fn lock(&self) -> KMutexGuard<'_, T> {
        let flags = cpu::disable_irq_save();
        self.lock.lock();
        KMutexGuard { mutex: self, flags }
    }

    /// 非阻塞尝试获取锁。
    pub fn try_lock(&self) -> Option<KMutexGuard<'_, T>> {
        let flags = cpu::disable_irq_save();
        if self.lock.try_lock() {
            Some(KMutexGuard { mutex: self, flags })
        } else {
            cpu::restore_irq(flags);
            None
        }
    }
}

/// RAII 守卫：离开作用域时自动释放锁 + 恢复中断。
pub struct KMutexGuard<'a, T> {
    mutex: &'a KMutex<T>,
    flags: u64,  // 保存的 DAIF 状态
}

impl<T> Deref for KMutexGuard<'_, T> {
    type Target = T;
    fn deref(&self) -> &T {
        unsafe { &*self.mutex.data.get() }
    }
}

impl<T> DerefMut for KMutexGuard<'_, T> {
    fn deref_mut(&mut self) -> &mut T {
        unsafe { &mut *self.mutex.data.get() }
    }
}

impl<T> Drop for KMutexGuard<'_, T> {
    fn drop(&mut self) {
        self.mutex.lock.unlock();
        cpu::restore_irq(self.flags);
    }
}
