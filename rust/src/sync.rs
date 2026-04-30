/// 内核互斥锁，基于关中断实现单核自旋。
/// 依赖 cpu 模块提供的中断开关。

use core::cell::UnsafeCell;
use core::ops::{Deref, DerefMut};
use crate::arch::aarch64::cpu;

pub struct KMutex<T> {
    data: UnsafeCell<T>,
}

// 内核中 KMutex 由关中断保证互斥，声明为 Sync / Send 安全。
unsafe impl<T: Send> Sync for KMutex<T> {}
unsafe impl<T: Send> Send for KMutex<T> {}

impl<T> KMutex<T> {
    /// `const fn` 构造函数，可在静态初始化中使用。
    pub const fn new(val: T) -> Self {
        KMutex {
            data: UnsafeCell::new(val),
        }
    }

    /// 获取锁，关闭中断防止抢占，返回守卫。
    pub fn lock(&self) -> KMutexGuard<'_, T> {
        cpu::disable_irq();
        KMutexGuard { mutex: self }
    }
}

pub struct KMutexGuard<'a, T> {
    mutex: &'a KMutex<T>,
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
        cpu::enable_irq();
    }
}