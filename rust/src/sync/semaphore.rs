use alloc::boxed::Box;
use alloc::collections::VecDeque;
use alloc::sync::Arc;
use core::cell::UnsafeCell;
use core::sync::atomic::Ordering;
use crate::arch::aarch64::cpu;
use crate::kernel::task::{Task, CURRENT_TASK};
use crate::kernel::scheduler;

/// 计数信号量。
///
/// `down` 在 count == 0 时会阻塞当前任务并调度出去，
/// `up` 会递增 count 并唤醒一个等待者。
///
/// 所有操作都通过关中断保证原子性（单核 UP 安全）。
pub struct Semaphore {
    count: UnsafeCell<isize>,
    queue: UnsafeCell<VecDeque<Arc<Task>>>,
}

unsafe impl Send for Semaphore {}
unsafe impl Sync for Semaphore {}

impl Semaphore {
    /// 创建一个新信号量，初始值为 `count`。
    ///
    /// 二进制信号量用 `Semaphore::new(1)`（此时等价于互斥锁）。
    pub const fn new(count: isize) -> Self {
        Semaphore {
            count: UnsafeCell::new(count),
            queue: UnsafeCell::new(VecDeque::new()),
        }
    }

    /// P 操作：获取信号量。
    ///
    /// 如果 count > 0，立即递减并返回；
    /// 否则将当前任务放入等待队列并调度出去。
    /// 被唤醒后重新竞争，保证不会丢失唤醒。
    pub fn down(&self) {
        loop {
            cpu::disable_irq();
            unsafe {
                if *self.count.get() > 0 {
                    *self.count.get() -= 1;
                    cpu::enable_irq();
                    return;
                }
                let task_ptr = CURRENT_TASK.load(Ordering::Relaxed);
                let task = &*task_ptr;
                let arc_self = (*task.arc_self.get()).as_ref().unwrap().clone();
                (*self.queue.get()).push_back(arc_self);
            }
            cpu::enable_irq();
            scheduler::schedule();
        }
    }

    /// V 操作：释放信号量。
    ///
    /// 递增 count。如果有任务在等待队列中，唤醒最早的一个。
    pub fn up(&self) {
        cpu::disable_irq();
        let task_to_wake = unsafe {
            *self.count.get() += 1;
            (*self.queue.get()).pop_front()
        };
        cpu::enable_irq();
        if let Some(task) = task_to_wake {
            scheduler::add_task(task);
        }
    }

    /// 非阻塞尝试获取信号量。
    ///
    /// 返回 `true` 表示获取成功（count 已递减），
    /// 返回 `false` 表示资源暂不可用。
    pub fn try_down(&self) -> bool {
        cpu::disable_irq();
        unsafe {
            if *self.count.get() > 0 {
                *self.count.get() -= 1;
                cpu::enable_irq();
                true
            } else {
                cpu::enable_irq();
                false
            }
        }
    }
}

// ============================================================
// FFI 导出，供 C 侧调用
// ============================================================

/// C 侧 opaque 句柄（仅指针，不关心内部布局）
/// C 侧只需前向声明 `struct semaphore;`，不暴露字段。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_sem_create(initial_count: isize) -> *mut Semaphore {
    unsafe { Box::into_raw(Box::new(Semaphore::new(initial_count))) }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_sem_destroy(sem: *mut Semaphore) {
    if !sem.is_null() {
        unsafe { drop(Box::from_raw(sem)) };
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_sem_down(sem: *mut Semaphore) {
    if let Some(s) = unsafe { sem.as_ref() } {
        s.down();
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_sem_up(sem: *mut Semaphore) {
    if let Some(s) = unsafe { sem.as_ref() } {
        s.up();
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_sem_try_down(sem: *mut Semaphore) -> i32 {
    unsafe { sem.as_ref().map_or(0, |s| s.try_down() as i32) }
}
