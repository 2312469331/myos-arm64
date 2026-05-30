use core::ops::{Deref, DerefMut};
use super::Semaphore;

/// 可睡眠阻塞的互斥锁。
///
/// 基于二进制信号量实现，在无法获取锁时睡眠等待，
/// 不会像 `KMutex` 那样关中断自旋。
pub struct Mutex {
    sem: Semaphore,
}

unsafe impl Send for Mutex {}
unsafe impl Sync for Mutex {}

impl Mutex {
    /// 创建一个未加锁的互斥锁。
    pub const fn new() -> Self {
        Mutex {
            sem: Semaphore::new(1),
        }
    }

    /// 获取互斥锁，必要时睡眠等待。
    pub fn lock(&self) -> MutexGuard<'_> {
        self.sem.down();
        MutexGuard { mutex: self }
    }

    /// 释放互斥锁。
    ///
    /// 安全要求：调用者必须持有该锁。
    pub fn unlock(&self) {
        self.sem.up();
    }

    /// 非阻塞尝试获取锁。
    ///
    /// 返回 `true` 表示获取成功。
    pub fn try_lock(&self) -> bool {
        self.sem.try_down()
    }
}

/// RAII 守卫，离开作用域时自动释放互斥锁。
pub struct MutexGuard<'a> {
    mutex: &'a Mutex,
}

impl Drop for MutexGuard<'_> {
    fn drop(&mut self) {
        self.mutex.sem.up();
    }
}
