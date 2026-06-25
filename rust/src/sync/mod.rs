/// 内核同步原语
///
/// - `KMutex` — Ticket Lock 自旋锁 + irqsave（多核安全、FIFO 公平）
/// - `Semaphore` — 计数信号量（可睡眠阻塞）
/// - `Mutex` — 互斥锁（基于信号量，可睡眠阻塞）

use core::cell::UnsafeCell;
use core::ops::{Deref, DerefMut};
use core::sync::atomic::{AtomicU32, AtomicBool, AtomicPtr, AtomicI32, Ordering};
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
//  QSpinlock — MCS 锁（多核缓存友好），适合 8+ 核
//  每个等待者只自旋自己的 local 标志，不争全局变量
// ============================================================

const MAX_CPUS: usize = 8;

#[repr(C, align(64))]
struct McsNode {
    next: AtomicPtr<McsNode>,
    locked: AtomicBool,
}

const fn mcs_init() -> UnsafeCell<McsNode> {
    UnsafeCell::new(McsNode {
        next: AtomicPtr::new(core::ptr::null_mut()),
        locked: AtomicBool::new(false),
    })
}

struct McsNodeArray([UnsafeCell<McsNode>; MAX_CPUS]);
unsafe impl Sync for McsNodeArray {}

static MCS_NODES: McsNodeArray = McsNodeArray([
    mcs_init(), mcs_init(), mcs_init(), mcs_init(),
    mcs_init(), mcs_init(), mcs_init(), mcs_init(),
]);

fn mcs_node() -> &'static mut McsNode {
    let id = cpu::cpu_id() % MAX_CPUS;
    unsafe { &mut *MCS_NODES.0[id].get() }
}

pub struct QSpinlock {
    tail: AtomicPtr<McsNode>,
}

unsafe impl Sync for QSpinlock {}
unsafe impl Send for QSpinlock {}

impl QSpinlock {
    pub const fn new() -> Self {
        QSpinlock {
            tail: AtomicPtr::new(core::ptr::null_mut()),
        }
    }

    /// 获取锁：把自己排到队尾，等前一个人拍肩膀。
    /// 如果没人等，直接拿锁。
    pub fn lock(&self) {
        let node = mcs_node();
        node.next.store(core::ptr::null_mut(), Ordering::Relaxed);
        node.locked.store(false, Ordering::Relaxed);

        let prev = self.tail.swap(node as *mut McsNode, Ordering::Acquire);
        if prev.is_null() {
            return; // 没人等，直接拿锁
        }

        // 挂到前一个人的 next 上
        unsafe { (*prev).next.store(node as *mut McsNode, Ordering::Release); }

        // 等前一个人拍肩膀（locked = true）
        while !node.locked.load(Ordering::Acquire) {
            core::hint::spin_loop();
        }
    }

    /// 非阻塞尝试获取锁。
    pub fn try_lock(&self) -> bool {
        let node = mcs_node();
        node.next.store(core::ptr::null_mut(), Ordering::Relaxed);
        node.locked.store(false, Ordering::Relaxed);

        self.tail.compare_exchange(
            core::ptr::null_mut(),
            node as *mut McsNode,
            Ordering::Acquire,
            Ordering::Relaxed,
        ).is_ok()
    }

    /// 释放锁：拍醒下一个等待者。
    /// 如果没人等，原子清 tail。
    pub fn unlock(&self) {
        let node = mcs_node();
        let n = node.next.load(Ordering::Relaxed);

        if n.is_null() {
            // 没人等，尝试清 tail
            if self.tail.compare_exchange(
                node as *mut McsNode,
                core::ptr::null_mut(),
                Ordering::Release,
                Ordering::Relaxed,
            ).is_ok() {
                return;
            }

            // 有人刚好排进来，等他设好 next 指针
            while node.next.load(Ordering::Relaxed).is_null() {
                core::hint::spin_loop();
            }
            let n = node.next.load(Ordering::Relaxed);
            unsafe { (*n).locked.store(true, Ordering::Release); }
            return;
        }

        // 拍醒下一个
        unsafe { (*n).locked.store(true, Ordering::Release); }
    }
}

// ============================================================
//  KMutex — QSpinlock + irqsave（替代原有关中断方案）
// ============================================================

/// 内核互斥锁：Ticket Lock 保证公平 + 关中断防止本地抢占。
///
/// - 单核：关中断足以互斥，Ticket Lock 是多余的但无害
/// - 多核：Ticket Lock 保证跨核互斥，关中断防止死锁
pub struct KMutex<T> {
    data: UnsafeCell<T>,
    lock: QSpinlock,
}

// 内核中 KMutex 由 TicketLock + 关中断保证互斥，声明为 Sync / Send 安全。
unsafe impl<T: Send> Sync for KMutex<T> {}
unsafe impl<T: Send> Send for KMutex<T> {}

impl<T> KMutex<T> {
    /// `const fn` 构造函数，可在静态初始化中使用。
    pub const fn new(val: T) -> Self {
        KMutex {
            data: UnsafeCell::new(val),
            lock: QSpinlock::new(),
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

// ============================================================
//  RwLock — 读写锁（写优先），适合读多写少场景
// ============================================================

/// 读写锁：多个读者可同时读，写者独占。
/// 写优先：有写者等待时，新的读者被阻止。
pub struct RwLock<T> {
    data: UnsafeCell<T>,
    state: AtomicI32,       // -1 = 写锁定，>=0 = 读者数
    write_wait: AtomicBool, // 有写者在等
}

unsafe impl<T: Send> Sync for RwLock<T> {}
unsafe impl<T: Send> Send for RwLock<T> {}

const WRITE_LOCKED: i32 = -1;

impl<T> RwLock<T> {
    pub const fn new(val: T) -> Self {
        RwLock {
            data: UnsafeCell::new(val),
            state: AtomicI32::new(0),
            write_wait: AtomicBool::new(false),
        }
    }

    /// 获取读锁。
    pub fn read(&self) -> RwLockReadGuard<'_, T> {
        loop {
            // 有写者在等或正在写 → 自旋
            if self.write_wait.load(Ordering::Relaxed) {
                core::hint::spin_loop();
                continue;
            }
            let s = self.state.load(Ordering::Relaxed);
            if s >= 0 && self.state.compare_exchange(
                s, s + 1,
                Ordering::Acquire,
                Ordering::Relaxed,
            ).is_ok() {
                break;
            }
        }
        RwLockReadGuard { rwlock: self }
    }

    /// 获取写锁。
    pub fn write(&self) -> RwLockWriteGuard<'_, T> {
        // 标记有写者在等（阻止新读者）
        self.write_wait.store(true, Ordering::Relaxed);

        // 等所有读者离开 → state 回到 0
        while self.state.compare_exchange(
            0, WRITE_LOCKED,
            Ordering::Acquire,
            Ordering::Relaxed,
        ).is_err() {
            core::hint::spin_loop();
        }

        RwLockWriteGuard { rwlock: self }
    }
}

/// RAII 读守卫
pub struct RwLockReadGuard<'a, T> {
    rwlock: &'a RwLock<T>,
}

impl<T> Deref for RwLockReadGuard<'_, T> {
    type Target = T;
    fn deref(&self) -> &T {
        unsafe { &*self.rwlock.data.get() }
    }
}

impl<T> Drop for RwLockReadGuard<'_, T> {
    fn drop(&mut self) {
        let prev = self.rwlock.state.fetch_sub(1, Ordering::Release);
        if prev == 1 && self.rwlock.write_wait.load(Ordering::Relaxed) {
            core::sync::atomic::fence(Ordering::SeqCst);
        }
    }
}

/// RAII 写守卫
pub struct RwLockWriteGuard<'a, T> {
    rwlock: &'a RwLock<T>,
}

impl<T> Deref for RwLockWriteGuard<'_, T> {
    type Target = T;
    fn deref(&self) -> &T {
        unsafe { &*self.rwlock.data.get() }
    }
}

impl<T> DerefMut for RwLockWriteGuard<'_, T> {
    fn deref_mut(&mut self) -> &mut T {
        unsafe { &mut *self.rwlock.data.get() }
    }
}

impl<T> Drop for RwLockWriteGuard<'_, T> {
    fn drop(&mut self) {
        self.rwlock.write_wait.store(false, Ordering::Relaxed);
        self.rwlock.state.store(0, Ordering::Release);
    }
}
