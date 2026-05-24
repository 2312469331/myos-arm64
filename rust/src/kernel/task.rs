/// 任务数据结构与工厂，包含自引用初始化。
/// 仅依赖 arch 模块和 alloc。

use alloc::boxed::Box;
use alloc::sync::Arc;
use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicPtr, Ordering};
use crate::arch::aarch64::context::{TaskContext, kthread_entry};

/// 内核线程控制块。
/// 栈使用固定大小 8 KiB，在堆上分配。
pub struct Task {
    pub stack: Box<[u8; 8192]>,
    pub context: UnsafeCell<TaskContext>,
    /// 自引用：指向自身 Arc。
    pub arc_self: UnsafeCell<Option<Arc<Task>>>,
    pub entry: fn(usize),
    pub arg: usize,
    pub name: &'static str,
}

// 内核线程可跨核共享，需实现 Send + Sync。
unsafe impl Send for Task {}
unsafe impl Sync for Task {}

/// 全局当前任务指针，以裸指针形式存储，配合 `Arc::into_raw` 管理。
pub static CURRENT_TASK: AtomicPtr<Task> = AtomicPtr::new(core::ptr::null_mut());

/// 创建一个新的内核线程。
/// 栈被分配，控制块自引用完后返回 Arc，
/// *注意*：调用者负责将线程加入就绪队列。
pub fn create_kernel_thread(
    entry: fn(usize),
    arg: usize,
    name: &'static str,
) -> Arc<Task> {
    let stack = Box::new([0u8; 8192]);
    // ARM 栈向下增长，sp 指向数组末端 + 1 并对齐 16 字节
    let sp_base = stack.as_ptr() as *const u8;
    let sp = (sp_base as usize + 8192) & !15; // 16 字节对齐
    let mut ctx = TaskContext::zeroed();
    ctx.sp = sp as u64;
    ctx.lr = kthread_entry as *const () as u64;  // ✅ 先转指针，再转 u64
    let task = Task {
        stack,
        context: UnsafeCell::new(ctx),
        arc_self: UnsafeCell::new(None),
        entry,
        arg,
        name,
    };

    let arc = Arc::new(task);
    // 自引用注入
    unsafe {
        (*arc.arc_self.get()) = Some(Arc::clone(&arc));
    }
    return arc;
}

/// 被 `kthread_entry` 调用的包装器。
/// 从 CURRENT_TASK 读取入口并执行，线程函数不应返回。
/// 若返回则进入死循环。
#[unsafe(no_mangle)]
pub extern "C" fn kthread_wrapper() {
    let task_ptr = CURRENT_TASK.load(Ordering::Relaxed);
    if task_ptr.is_null() {
        // 若出现此情况则无法恢复，直接挂起。
        loop {}
    }
    let task = unsafe { &*task_ptr };
    let entry = task.entry;
    let arg = task.arg;
    entry(arg);

    // 线程函数若返回，挂起 CPU。
    loop {
        core::hint::spin_loop();
    }
}