
/// 等待队列 + 线程延时实现。
/// 依赖 task 与 scheduler，但绝不引入 scheduler 的循环依赖。

use alloc::collections::VecDeque;
use alloc::sync::Arc;
use core::cell::UnsafeCell;
use core::sync::atomic::Ordering;
use crate::kernel::task::{self, Task, CURRENT_TASK};
use crate::kernel::scheduler;
use crate::sync::KMutex;

/// 定时器频率，与 C 侧 TICK_HZ 保持一致（1000 Hz = 1ms/tick）
const TICK_HZ: u64 = 1000;

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
        let arc_self = unsafe { (*task.arc_self.get()).as_ref().unwrap().upgrade().unwrap() };
        // 设置状态为 SLEEPING，schedule() 不会将此任务放回就绪队列
        task.state.store(task::TASK_SLEEPING, Ordering::Relaxed);
        guard.push_back(arc_self);
        // 务必在调度前释放锁！
        drop(guard);
        // 如果 wake_up 在 drop(guard) 后抢先执行了（state=RUNNING），
        // 它已 add_task，此时调 schedule() 会重复入队→双调度
        if task.state.load(Ordering::Relaxed) == task::TASK_SLEEPING {
            scheduler::schedule();
        }
        // 被唤醒后会重新回到循环顶部，检查条件。
    }
}

/// 唤醒等待队列中的所有任务，将其转移到就绪队列。
pub fn wake_up(wq: &WaitQueue) {
    let mut guard = wq.queue.lock();
    while let Some(task) = guard.pop_front() {
        // 设置状态为 RUNNING，允许 schedule() 将此任务放入就绪队列
        task.state.store(task::TASK_RUNNING, Ordering::Relaxed);
        scheduler::add_task_ready(task);
    }
}

// ============================================================
//  线程延时（sleep）
// ============================================================

/// sleep 队列条目：任务 + 唤醒时间点。
struct SleepEntry {
    task: Arc<Task>,
    expire_tick: u64,
}

/// 全局 sleep 队列，按 expire_tick 升序排列。
/// 定时器中断里只读头部，task 上下文插入时需关中断保护。
struct SleepQueueInner(UnsafeCell<VecDeque<SleepEntry>>);
unsafe impl Sync for SleepQueueInner {}
static SLEEP_QUEUE: SleepQueueInner = SleepQueueInner(UnsafeCell::new(VecDeque::new()));

unsafe extern "C" {
    static system_tick: u64;
}

/// 让当前线程睡眠 `ms` 毫秒。
///
/// 实现：
/// 1. 计算 expire_tick = system_tick + ms对应的tick数
/// 2. 关中断，将任务按过期时间有序插入 sleep 队列
/// 3. 设状态 SLEEPING，调度出去
/// 4. 定时器中断里 check_sleepers 到期后唤醒
pub fn sleep(ms: u32) {
    if ms == 0 {
        return;
    }

    let ticks = (ms as u64 * TICK_HZ) / 1000;
    let expire_tick = unsafe { system_tick } + ticks;

    let irq_flags = crate::arch::aarch64::cpu::disable_irq_save();

    let task_ptr = CURRENT_TASK.load(Ordering::Relaxed);
    let task = unsafe { &*task_ptr };
    let arc_self = unsafe { (*task.arc_self.get()).as_ref().unwrap().upgrade().unwrap() };

    // 设状态为 SLEEPING
    task.state.store(task::TASK_SLEEPING, Ordering::Relaxed);

    // 按 expire_tick 升序插入 sleep 队列
    let entry = SleepEntry {
        task: arc_self,
        expire_tick,
    };
    let q = unsafe { &mut *SLEEP_QUEUE.0.get() };
    let pos = q.iter().position(|e| e.expire_tick > expire_tick).unwrap_or(q.len());
    q.insert(pos, entry);

    crate::arch::aarch64::cpu::restore_irq(irq_flags);

    // 调度出去，醒来后 sleep() 返回
    scheduler::schedule();
}

/// 定时器中断里调用：检查 sleep 队列，唤醒到期的任务。
///
/// 由 timer_irq_handler 每 tick 调用一次。
/// 此函数在 IRQ 上下文中执行，不需要额外加锁（IRQ 已禁用）。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_check_sleepers() {
    let now = unsafe { system_tick };
    let q = unsafe { &mut *SLEEP_QUEUE.0.get() };

    // 按序扫描，到期的全部唤醒
    while let Some(entry) = q.front() {
        if now >= entry.expire_tick {
            let entry = q.pop_front().unwrap();
            entry.task.state.store(task::TASK_RUNNING, Ordering::Relaxed);
            scheduler::add_task_ready(entry.task);
        } else {
            break; // 后面的都还没到期，不用查了
        }
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
        sleep(50);
    }
    // 永久等待
    wait_event(&TEST_WQ, || false);
}

/// 测试 sleep 功能的线程
fn sleeper_func(_: usize) {
    println!("[SLEEPER] Start, sleep 300ms...");
    sleep(300);
    println!("[SLEEPER] Woke up after 300ms!");
    sleep(200);
    println!("[SLEEPER] Woke up after another 200ms!");
    // 永久等待
    wait_event(&TEST_WQ, || false);
}

// -----------------------------------------------------------------
//  信号量 + 互斥锁 测试
// -----------------------------------------------------------------

use crate::sync::{Semaphore, Mutex};

/// 共享资源：模拟 3 个车位的停车场
static PARKING_SEM: Semaphore = Semaphore::new(3);
static PARKING_COUNT: AtomicUsize = AtomicUsize::new(0);

/// 信号量测试线程：模拟停车
fn sem_tester(id: usize) {
    println!("[SEM-{}] wants to park", id);
    PARKING_SEM.down();  // 获取车位
    let current = PARKING_COUNT.fetch_add(1, MemOrder::Relaxed) + 1;
    println!("[SEM-{}] parked!  cars inside: {}", id, current);
    sleep(200);  // 停 200ms
    let current = PARKING_COUNT.fetch_sub(1, MemOrder::Relaxed) - 1;
    println!("[SEM-{}] leaving   cars inside: {}", id, current);
    PARKING_SEM.up();  // 释放车位
    // 永久等待
    wait_event(&TEST_WQ, || false);
}

/// 互斥锁测试：全局计数器，多线程竞争递增
static COUNTER_MUTEX: Mutex = Mutex::new();
static COUNTER: AtomicUsize = AtomicUsize::new(0);

/// 互斥锁测试线程：安全递增计数器
fn mutex_tester(id: usize) {
    for _ in 0..5 {
        {
            let _guard = COUNTER_MUTEX.lock();  // RAII 加锁
            let val = COUNTER.load(MemOrder::Relaxed) + 1;
            COUNTER.store(val, MemOrder::Relaxed);
            println!("[MUTEX-{}] counter = {}", id, val);
        }  // ← guard 在这里 drop，释放锁
        sleep(50);  // 临界区外 sleep 50ms，让其他线程有机会拿锁
    }
    // 永久等待
    wait_event(&TEST_WQ, || false);
}

/// KMutex（TicketLock）测试：验证多线程不丢数据
static TICKET_MUTEX: crate::sync::KMutex<usize> = crate::sync::KMutex::new(0);
static TICKET_COUNT: AtomicUsize = AtomicUsize::new(0);

/// TicketLock 测试线程
fn ticket_tester(id: usize) {
    for _ in 0..3 {
        let mut guard = TICKET_MUTEX.lock();
        *guard += 1;
        let val = *guard;
        drop(guard);  // 手动提前释放
        TICKET_COUNT.fetch_add(1, MemOrder::Relaxed);
        println!("[TICKET-{}] shared={}, total_ops={}", id, val, TICKET_COUNT.load(MemOrder::Relaxed));
        sleep(30);
    }
    wait_event(&TEST_WQ, || false);
}

/// 启动同步原语测试
pub fn start_sync_test() {
    // 信号量测试：5 个线程抢 3 个车位
    for i in 0..5 {
        let t = crate::kernel::task::create_kernel_thread(sem_tester, i, "sem_test", 5);
        scheduler::add_task(t);
    }
    // 互斥锁测试：3 个线程竞争递增
    for i in 0..3 {
        let t = crate::kernel::task::create_kernel_thread(mutex_tester, i, "mutex_test", 5);
        scheduler::add_task(t);
    }
    // TicketLock 测试：4 个线程竞争
    for i in 0..4 {
        let t = crate::kernel::task::create_kernel_thread(ticket_tester, i, "ticket_test", 5);
        scheduler::add_task(t);
    }
    // 时间片轮转测试：3 个线程各打印 5 轮，观察交替执行
    for i in 0..3 {
        let t = crate::kernel::task::create_kernel_thread(rr_tester, i, "rr_test", 5);
        scheduler::add_task(t);
    }
    // 优先级测试：不同优先级线程，观察谁先执行
    // prio: 7=中, 8=高, 9=最高（都高于其他测试线程的 5）
    for i in 0..3 {
        let prio = 7 + i; // 7, 8, 9
        let t = crate::kernel::task::create_kernel_thread(prio_tester, prio, "prio_test", prio);
        scheduler::add_task(t);
    }
}

/// 时间片轮转测试：每个线程打印自己的 ID 和轮次，观察是否交替执行。
/// 3 个线程各打印 5 轮，如果轮转正常，输出应该是交错的。
fn rr_tester(id: usize) {
    for round in 0..5 {
        crate::print!("RR[{}] round {} | ", id, round);
        for _ in 0..200 {
            crate::kernel::scheduler::yield_if_need();
        }
    }
    crate::println!("RR[{}] done!", id);
    loop {
        core::hint::spin_loop();
    }
}

/// 优先级测试：每个线程打印后返回（验证线程退出回收）。
fn prio_tester(prio: usize) {
    crate::println!("[PRIO-{}] start (prio={})", prio, prio);
    for _ in 0..3 {
        for _ in 0..50 {
            crate::kernel::scheduler::yield_if_need();
        }
        crate::print!("PRIO-{} | ", prio);
    }
    crate::println!("[PRIO-{}] done! (returning)", prio);
    // 返回后 kthread_wrapper 会设 TASK_DEAD 并 schedule
}

/// 由 `rust_main` 调用的测试启动函数。
pub fn start_wait_queue_test() {
    let waiter = crate::kernel::task::create_kernel_thread(waiter_func, 0, "waiter", 5);
    scheduler::add_task(waiter);
    let waker = crate::kernel::task::create_kernel_thread(waker_func, 0, "waker", 5);
    scheduler::add_task(waker);
    let sleeper = crate::kernel::task::create_kernel_thread(sleeper_func, 0, "sleeper", 5);
    scheduler::add_task(sleeper);
    // 同步原语测试
    start_sync_test();
}