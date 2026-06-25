use alloc::boxed::Box;
use alloc::collections::VecDeque;
use alloc::sync::Arc;
use core::sync::atomic::Ordering;
use crate::kernel::task::{self, Task, CURRENT_TASK};
use crate::kernel::scheduler;
use super::KMutex;

struct SemInner {
    count: isize,
    queue: VecDeque<Arc<Task>>,
    owner: Option<Arc<Task>>,  // 当前持有者（优先级继承用）
}

/// 计数信号量（多核安全）。
///
/// `down` 在 count == 0 时会阻塞当前任务并调度出去，
/// `up` 会递增 count 并唤醒一个等待者。
///
/// 内部通过 KMutex（TicketLock + disable_irq）保证原子性。
pub struct Semaphore {
    inner: KMutex<SemInner>,
}

unsafe impl Send for Semaphore {}
unsafe impl Sync for Semaphore {}

impl Semaphore {
    pub const fn new(count: isize) -> Self {
        Semaphore {
            inner: KMutex::new(SemInner {
                count,
                queue: VecDeque::new(),
                owner: None,
            }),
        }
    }

    /// P 操作：获取信号量。
    ///
    /// 如果 count > 0，立即递减并返回；
    /// 否则将当前任务设为 SLEEPING 放入等待队列，释放锁后调度出去。
    /// 实现优先级继承：当高优先级任务因低优先级任务持有信号量而阻塞时，
    /// 临时提升持有者的优先级。
    pub fn down(&self) {
        loop {
            {
                let mut inner = self.inner.lock();
                if inner.count > 0 {
                    inner.count -= 1;
                    // 跟踪持有者
                    let cur = CURRENT_TASK.load(Ordering::Relaxed);
                    let arc = unsafe { (*((*cur).arc_self.get())).as_ref().unwrap().upgrade().unwrap() };
                    inner.owner = Some(arc);
                    return;
                }
                let task_ptr = CURRENT_TASK.load(Ordering::Relaxed);
                let task = unsafe { &*task_ptr };
                let arc_self = unsafe { (*task.arc_self.get()).as_ref().unwrap().upgrade().unwrap() };

                // 优先级继承：如果持有者的优先级低于当前任务，提升持有者
                if let Some(ref owner) = inner.owner {
                    if task.prio > owner.prio {
                        owner.original_prio.store(owner.prio, Ordering::Relaxed);
                        let owner_ptr = Arc::as_ptr(owner) as *mut Task;
                        unsafe { (*owner_ptr).prio = task.prio; }
                    }
                }

                inner.queue.push_back(arc_self);
                task.state.store(task::TASK_SLEEPING, Ordering::Relaxed);
            }
            scheduler::schedule();
        }
    }

    /// V 操作：释放信号量。
    ///
    /// 递增 count。如果有任务在等待队列中，唤醒最早的一个。
    /// 同时恢复持有者的原始优先级（优先级继承）。
    pub fn up(&self) {
        let task_to_wake = {
            let mut inner = self.inner.lock();
            inner.count += 1;

            // 恢复持有者的原始优先级
            if let Some(ref owner) = inner.owner {
                let orig = owner.original_prio.load(Ordering::Relaxed);
                if owner.prio != orig {
                    let owner_ptr = Arc::as_ptr(owner) as *mut Task;
                    unsafe { (*owner_ptr).prio = orig; }
                }
            }
            inner.owner = None;

            inner.queue.pop_front()
        };
        if let Some(task) = task_to_wake {
            task.state.store(task::TASK_RUNNING, Ordering::Relaxed);
            scheduler::add_task(task);
        }
    }

    /// 非阻塞尝试获取信号量。
    pub fn try_down(&self) -> bool {
        let mut inner = self.inner.lock();
        if inner.count > 0 {
            inner.count -= 1;
            true
        } else {
            false
        }
    }
}

// ============================================================
// FFI 导出，供 C 侧调用
// ============================================================

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
