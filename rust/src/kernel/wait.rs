
/// 等待队列实现，包含闭环测试代码。
/// 依赖 task 与 scheduler，但绝不引入 scheduler 的循环依赖。

use alloc::collections::VecDeque;
use alloc::sync::Arc;
use core::sync::atomic::Ordering;
use crate::kernel::task::{Task, CURRENT_TASK};
use crate::kernel::scheduler;
use crate::sync::KMutex;

/// 等待队列，基于 KMutex + VecDeque。
pub struct WaitQueue {
    queue: KMutex<VecDeque<Arc<Task>>>,
}

impl WaitQueue {
    /// 必须为 `const fn`，以便静态初始化。
    pub const fn new() -> Self {
        WaitQueue {
            queue: KMutex::new(VecDeque::new()),
        }
    }
}

/// 在等待队列上阻塞当前任务，直到条件满足。
/// 典型用法：循环内加锁检查，不满足则放回等待队列并调度。
pub fn wait_event(wq: &WaitQueue, condition: impl Fn() -> bool) {
    loop {
        let mut guard = wq.queue.lock();
        if condition() {
            break;
        }
        // 获取当前任务的自引用 Arc
        let task_ptr = CURRENT_TASK.load(Ordering::Relaxed);
        let task = unsafe { &*task_ptr };
        let arc_self = unsafe { (*task.arc_self.get()).as_ref().unwrap().clone() };
        guard.push_back(arc_self);
        // 务必在调度前释放锁！
        drop(guard);
        scheduler::schedule();
        // 被唤醒后会重新回到循环顶部，检查条件。
    }
}

/// 唤醒等待队列中的所有任务，将其转移到就绪队列。
pub fn wake_up(wq: &WaitQueue) {
    let mut guard = wq.queue.lock();
    while let Some(task) = guard.pop_front() {
        scheduler::add_task(task);
    }
}

// -----------------------------------------------------------------
//  闭环测试代码，全部写在本文件底部，不造成循环依赖
// -----------------------------------------------------------------

use core::sync::atomic::{AtomicUsize, Ordering as MemOrder};
use crate::println;

static TEST_WQ: WaitQueue = WaitQueue::new();
static COUNT: AtomicUsize = AtomicUsize::new(0);

fn waiter_func(_: usize) {
    // 等待计数 > 2
    wait_event(&TEST_WQ, || {
        COUNT.fetch_add(1, MemOrder::Relaxed) > 2
    });
    println!("Waiter woke up!");
    // 主动让出，测试 yield_now
    scheduler::yield_now();
    // 永久等待
    wait_event(&TEST_WQ, || false);
}

fn waker_func(_: usize) {
    for i in 0..3 {
        println!("Waker: tick {}", i);
        COUNT.fetch_add(1, MemOrder::Relaxed);
        wake_up(&TEST_WQ);
        scheduler::yield_now();
    }
    // 永久等待
    wait_event(&TEST_WQ, || false);
}

/// 由 `rust_main` 调用的测试启动函数。
pub fn start_wait_queue_test() {
    let waiter = crate::kernel::task::create_kernel_thread(waiter_func, 0, "waiter");
    scheduler::add_task(waiter);
    let waker = crate::kernel::task::create_kernel_thread(waker_func, 0, "waker");
    scheduler::add_task(waker);
    // println!("[test] WaitQueue test threads created.");
}