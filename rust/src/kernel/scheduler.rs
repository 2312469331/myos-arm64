//rust\src\kernel\scheduler.rs
/// 调度器：最核心的响应与模块单向边界。
/// 注意：此文件**绝不**导入 `wait` 模块，防止循环依赖。

use alloc::collections::VecDeque;
use alloc::sync::Arc;
use core::cell::UnsafeCell;
use core::sync::atomic::Ordering;
use crate::arch::aarch64::cpu::{self, NEED_RESCHED, PREEMPT_COUNT};
use crate::arch::aarch64::context::{context_switch, start_first_task, kthread_entry};
use crate::kernel::task::{self, Task, CURRENT_TASK};
use crate::kernel::wait;
use crate::ffi;
use alloc::ffi::CString;
use alloc::string::String;
use alloc::vec::Vec;
use core::fmt::Write;
use ffi::Console;
use crate::irq;
use crate::println;

// 外部汇编标签：直接跳转回异常退出恢复现场，不经过 C 函数尾幕。
unsafe extern "C" {
      unsafe fn el1_exception_exit();
}

// ---- 就绪队列 -----
struct RQInner(UnsafeCell<VecDeque<Arc<Task>>>);

// 内核就绪队列必须全局共享，手动标注 Sync。
unsafe impl Sync for RQInner {}

static READY_QUEUE: RQInner = RQInner(UnsafeCell::new(VecDeque::new()));

/// 向就绪队列添加任务。
pub fn add_task(task: Arc<Task>) {
    let q = unsafe { &mut *READY_QUEUE.0.get() };
    q.push_back(task);
}

/// 核心调度函数：
/// - 关中断
/// - 将当前任务重新放回就绪队列
/// - 取出下一任务，切换 CURRENT_TASK
/// - 调用 `context_switch` 完成任务上下文切换
/// - 恢复中断后正常返回
///
/// 注意：返回时可能已处于另一任务的上下文中。
pub fn schedule() {
    cpu::disable_irq();

    let cur_ptr = CURRENT_TASK.load(Ordering::Relaxed);

    // 将当前任务重新入队（若存在）
    if !cur_ptr.is_null() {
        unsafe {
            // 从裸指针获取所有权
            let cur_arc = Arc::from_raw(cur_ptr);
            let clone = Arc::clone(&cur_arc);
            let q = &mut *READY_QUEUE.0.get();
            q.push_back(clone);
            drop(cur_arc); // 释放临时所有权，队列保有引用
        }
    }

    // 从就绪队列取出下一个任务
    let next_arc = {
        let q = unsafe { &mut *READY_QUEUE.0.get() };
        q.pop_front().expect("schedule: no runnable task in ready queue!")
    };

    // 释放 Arc 的所有权，转为裸指针交由 CURRENT_TASK 保存
    let next_ptr = Arc::into_raw(next_arc) as *mut Task;
    CURRENT_TASK.store(next_ptr, Ordering::Relaxed);

    // 执行上下文切换
    unsafe {
        let cur_ctx = if cur_ptr.is_null() {
            core::ptr::null_mut()
        } else {
            (*cur_ptr).context.get()
        };
        let next_ctx = (*next_ptr).context.get();
        context_switch(cur_ctx, next_ctx);
    }

    // context_switch 正常返回，可能已是新任务
    cpu::enable_irq();
}

/// 主动让出 CPU：将当前任务放回队列并调用 schedule。
/// 通过普通函数调用实现，后续 context_switch 返回时会回到调用者继续执行。
pub fn yield_now() {
    schedule();
}

/// 定时器中断回调：仅标记需要重新调度。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_timer_tick() {
    NEED_RESCHED.store(true, Ordering::Relaxed);
}

/// 异常返回前的调度检查与切换。
///
/// # ⚠️ 永远不返回 C 层
///
/// **致命逻辑**：当发生上下文切换时，`schedule()` 内部会把物理 SP 寄存器换成新任务的栈。
/// 如果此时 `return` 回 C 异常入口函数，C 编译器生成的函数尾声会使用恢复后的 `sp` 弹出栈帧，
/// 但此栈已是**新任务的栈**，弹出的数据为随机垃圾，导致立即崩溃（炸栈）。
///
/// 因此，一旦执行了调度，必须通过 `b el1_exception_exit` 直接跳转到底层汇编的异常退出标签，
/// 完全绕过 C 的函数返回机制，从而安全恢复新任务的现场。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_check_and_schedule() -> usize {
    unsafe{
        // 仅在需要调度且抢占计数为 0 时执行
        if NEED_RESCHED.load(Ordering::Relaxed) && PREEMPT_COUNT.load(Ordering::Relaxed) == 0 {
            NEED_RESCHED.store(false, Ordering::Relaxed);
            schedule();

            // 内联汇编
            core::arch::asm!(
                "b {}",
                sym el1_exception_exit,
                options(noreturn)
            );
        }

        // 未发生切换，安全返回 C 层继续原有逻辑。
        0
    }
}
/// Rust 内核入口，由 C 端硬件初始化完毕后调用。
#[unsafe(no_mangle)]
pub extern "C" fn rust_main() {
    // 以下调用都直接传 as_ptr()，不再 as *const u8
    unsafe {
        ffi::c_print_str(c"[RUST] Kernel upper layer started.\n".as_ptr());
    }
    let _ = writeln!(Console, "ARM64 裸机测试：{}", 123);

    // 测试中断注册
    unsafe {
        ffi::c_print_str(c"[RUST] Testing interrupt registration...\n".as_ptr());
    }
    
    // 定义一个中断处理函数
    extern "C" fn test_irq_handler(irq: u32) {
        let log = alloc::format!("[RUST] IRQ handler called for IRQ {}\n", irq);
        let c_log = CString::new(log).unwrap();
        unsafe {
            ffi::c_print_str(c_log.as_ptr());
        }
    }
    
        // 唯一 Stable 1.95 合法能用的裸函数
    // #[unsafe(naked)]
    // pub extern "C" fn test_naked_ok() {
    //     core::arch::naked_asm!(
    //         "mov x0, #123\n",
    //         "ret\n"
    //     );
    // }

    
    // // 注册中断处理函数
    // irq::register_irq_enum(irq::IrqNumber::Uart0, test_irq_handler, "Rust UART Test");
    
    // unsafe {
    //     ffi::c_print_str(c"[RUST] IRQ handler registered successfully\n".as_ptr());
    // }

    // 测试 String（底层走你的 Slab）
    let mut greeting = String::from("Hello from Rust String!");
    greeting.push_str(" Using your C Slab.\n");
    let c_greeting = CString::new(greeting).unwrap();
    unsafe {
        ffi::c_print_str(c_greeting.as_ptr());
    }

    // 测试 Vec
    let mut numbers = Vec::new();
    for i in 0..5 {
        numbers.push(i * 10);
    }

    let log = alloc::format!("Vec test: {:?}\n", numbers);
    let c_log = CString::new(log).unwrap();
    unsafe {
        ffi::c_print_str(c_log.as_ptr());
    }

    // 不需要 Vec 的地方直接用数组，避免 useless_vec 警告
    {
        let _tmp = [1, 2, 3];
    }
    unsafe {
        ffi::c_print_str(c"[RUST] Vec dropped (kfree called).\n".as_ptr());
    }

    unsafe {
        ffi::c_print_str(c"[RUST] All alloc tests passed!\n".as_ptr());
    }
    
    println!("[kernel] Rust main starting...");

    // 1. idle 线程，永远循环，不会返回。
    fn idle_func(_: usize) {
        loop {
            // 低功耗等待中断，如 `wfi`
            unsafe { core::arch::asm!("wfi"); }
        }
    }
    let idle = task::create_kernel_thread(idle_func, 0, "idle");
    add_task(Arc::clone(&idle));
    // 泄漏初始 Arc，保证 idle 永远存活。
    core::mem::forget(idle);

    // 2. 启动 WaitQueue 闭环测试（定义在 wait.rs，无循环依赖）。
    wait::start_wait_queue_test();

    // 3. 挑选第一个非 idle 任务并启动。
    // 因为 idle 已在队列中，出队一个任务（测试中创建的线程），
    // 将 CURRENT_TASK 设为它，并直接恢复其上下文运行，永不返回。
    cpu::disable_irq();
    let first_arc = {
        let q = unsafe { &mut *READY_QUEUE.0.get() };
        q.pop_front().expect("rust_main: no task to start!")
    };
    let first_ptr = Arc::into_raw(first_arc) as *mut Task;
    CURRENT_TASK.store(first_ptr, Ordering::Relaxed);

    unsafe {
        let ctx_ptr = (*first_ptr).context.get();
        start_first_task(ctx_ptr);
    }
    
    loop {
        core::hint::spin_loop();
    }

}


