//rust\src\kernel\scheduler.rs
/// 调度器：最核心的响应与模块单向边界。
/// 注意：此文件**绝不**导入 `wait` 模块，防止循环依赖。

use alloc::boxed::Box;
use alloc::collections::VecDeque;
use alloc::sync::Arc;
use core::cell::UnsafeCell;
use core::sync::atomic::Ordering;
use crate::arch::aarch64::cpu::{self, NEED_RESCHED, PREEMPT_COUNT};
use crate::kernel::pgtbl;
use crate::arch::aarch64::context::{context_switch, start_first_task, kthread_entry, TaskContext};
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

// // 外部汇编标签：直接跳转回异常退出恢复现场，不经过 C 函数尾幕。
// unsafe extern "C" {
//       unsafe fn el1_exception_exit();
// }

// ---- 优先级就绪队列（0-9，数字越大优先级越高） -----
struct RQInner(UnsafeCell<[VecDeque<Arc<Task>>; task::PRIO_NR]>);

// 内核就绪队列必须全局共享，手动标注 Sync。
unsafe impl Sync for RQInner {}

static READY_QUEUE: RQInner = RQInner(UnsafeCell::new([
    VecDeque::new(), VecDeque::new(), VecDeque::new(), VecDeque::new(), VecDeque::new(),
    VecDeque::new(), VecDeque::new(), VecDeque::new(), VecDeque::new(), VecDeque::new(),
]));

/// 全局任务列表（用于 kill 按 PID 查找）
pub(crate) static ALL_TASKS: Mutex = Mutex::new();

pub(crate) struct Mutex {
    lock: core::sync::atomic::AtomicBool,
    tasks: UnsafeCell<Vec<Arc<Task>>>,
    saved: UnsafeCell<u64>, // 保存中断状态
}

unsafe impl Sync for Mutex {}

impl Mutex {
    const fn new() -> Self {
        Self {
            lock: core::sync::atomic::AtomicBool::new(false),
            tasks: UnsafeCell::new(Vec::new()),
            saved: UnsafeCell::new(0),
        }
    }

    fn irq_save() -> u64 {
        let flags: u64;
        unsafe { core::arch::asm!("mrs {}, daif", out(reg) flags); }
        unsafe { core::arch::asm!("msr daifset, #2"); } // 关 IRQ
        flags
    }

    fn irq_restore(flags: u64) {
        unsafe { core::arch::asm!("msr daif, {}", in(reg) flags); }
    }

    fn lock(&self) -> &UnsafeCell<Vec<Arc<Task>>> {
        let flags = Self::irq_save();
        unsafe { *self.saved.get() = flags; }
        while self.lock.compare_exchange_weak(
            false, true,
            Ordering::Acquire, Ordering::Relaxed
        ).is_err() {
            core::hint::spin_loop();
        }
        &self.tasks
    }

    pub(crate) fn unlock(&self) {
        self.lock.store(false, Ordering::Release);
        let flags = unsafe { *self.saved.get() };
        Self::irq_restore(flags);
    }

    pub(crate) fn lock_and_get(&self) -> *mut Vec<Arc<Task>> {
        let flags = Self::irq_save();
        unsafe { *self.saved.get() = flags; }
        while self.lock.compare_exchange_weak(
            false, true,
            Ordering::Acquire, Ordering::Relaxed
        ).is_err() {
            core::hint::spin_loop();
        }
        self.tasks.get()
    }
}

/// 注册任务到全局列表
pub fn register_task(task: Arc<Task>) {
    let tasks = ALL_TASKS.lock();
    unsafe { &mut *tasks.get() }.push(task);
    ALL_TASKS.unlock();
}

/// 加锁遍历全局任务列表，返回匹配条件的第一个僵尸子进程（供 do_wait4 使用）
pub fn find_zombie_child(parent_pid: usize, target_pid: i64) -> Option<(i32, usize)> {
    let p = ALL_TASKS.lock_and_get();
    let tasks = unsafe { &mut *p };

    // 先清理已回收的 TASK_DEAD（release 掉 arc 资源）
    tasks.retain(|t| t.state.load(Ordering::Relaxed) != task::TASK_DEAD);

    // 找僵尸子进程
    for task in tasks.iter() {
        if task.ppid == parent_pid && task.state.load(Ordering::Relaxed) == task::TASK_ZOMBIE {
            if target_pid < 0 || task.pid == target_pid as usize {
                let result = Some((task.exit_code, task.pid));
                task.state.store(task::TASK_DEAD, Ordering::Relaxed);
                // 释放 self-arc，允许 Task::drop 回收页表/物理页
                unsafe { *task.arc_self.get() = None; }
                ALL_TASKS.unlock();
                return result;
            }
        }
    }
    ALL_TASKS.unlock();
    None
}

/// 按 PID 查找任务
pub fn find_task_by_pid(pid: usize) -> Option<Arc<Task>> {
    let tasks = ALL_TASKS.lock();
    let result = unsafe { &*tasks.get() }.iter()
        .find(|t| t.pid == pid)
        .cloned();
    ALL_TASKS.unlock();
    result
}

/// 向就绪队列和全局列表添加任务（首次创建时调用）。
pub fn add_task(task: Arc<Task>) {
    let prio = task.prio;
    let q = unsafe { &mut *READY_QUEUE.0.get() };
    q[prio].push_back(Arc::clone(&task));
    register_task(task);
}

/// 只加入就绪队列，不碰 ALL_TASKS（唤醒已有任务时用，避免 IRQ 死锁）
pub fn add_task_ready(task: Arc<Task>) {
    let prio = task.prio;
    let q = unsafe { &mut *READY_QUEUE.0.get() };
    q[prio].push_back(task);
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
    let cur_ptr = CURRENT_TASK.load(Ordering::Relaxed);

    // 拿回当前任务的 Arc 所有权（保留到 context_switch 之后释放）
    let cur_arc = if !cur_ptr.is_null() {
        unsafe { Some(Arc::from_raw(cur_ptr)) }
    } else {
        None
    };

    // 将当前任务重新入队（仅在 RUNNING 状态时）
    if let Some(ref arc) = cur_arc {
        unsafe {
            if (*cur_ptr).state.load(Ordering::Relaxed) == task::TASK_RUNNING {
                let clone = Arc::clone(arc);
                let prio = (*cur_ptr).prio;
                let q = &mut *READY_QUEUE.0.get();
                q[prio].push_back(clone);
            }
        }
    }

    // 从最高优先级队列取出下一个任务
    let next_arc = {
        let q = unsafe { &mut *READY_QUEUE.0.get() };
        let mut found = None;
        for prio in (0..task::PRIO_NR).rev() {
            if let Some(task) = q[prio].pop_front() {
                found = Some(task);
                break;
            }
        }
        found.expect("schedule: no runnable task in ready queue!")
    };

    let next_ptr = Arc::into_raw(next_arc) as *mut Task;
    CURRENT_TASK.store(next_ptr, Ordering::Relaxed);

    // 更新 C 侧的 current_mm（用于 page fault 处理）
    unsafe {
        unsafe extern "C" {
            static mut current_mm: *mut crate::ffi::MmStruct;
        }
        current_mm = crate::kernel::task::current_mm_ptr();
    }

    // 切换页表：用户进程有独立的 TTBR0 页表
    unsafe {
        let next_pgd = (*next_ptr).pgd.as_pa();
        let cur_pgd = if cur_ptr.is_null() { 0 } else { (*cur_ptr).pgd.as_pa() };
        if next_pgd != 0 && next_pgd != cur_pgd {
            pgtbl::switch_page_table(next_pgd);
        }
    }

    // 获取上下文指针（必须在 context_switch 之前）
    let cur_ctx = if cur_ptr.is_null() {
        core::ptr::null_mut()
    } else {
        unsafe { (*cur_ptr).context.get() }
    };
    let next_ctx = unsafe { (*next_ptr).context.get() };

    // 调试：打印当前/下一个任务的关键寄存器
    if crate::config::SCHED_DEBUG {
    unsafe {
        let mut buf = [0u8; 200];
        let mut pos = 0;
        
        // 当前任务
        if !cur_ptr.is_null() {
            let cur = &*cur_ptr;
            let ctx = &*cur.context.get();
            let s = b"[S] cur=";
            buf[pos..pos+8].copy_from_slice(s); pos += 8;
            let pid = cur.pid;
            if pid >= 10 { buf[pos] = b'0' + (pid/10) as u8; pos += 1; }
            buf[pos] = b'0' + (pid%10) as u8; pos += 1;
            buf[pos] = b' '; pos += 1;
            buf[pos..pos+4].copy_from_slice(b"lr="); pos += 4;
            for i in (0..64).step_by(4).rev() {
                let d = ((ctx.lr >> i) & 0xf) as u8;
                buf[pos] = if d < 10 { b'0'+d } else { b'a'+d-10 }; pos += 1;
            }
            buf[pos] = b' '; pos += 1;
            buf[pos..pos+5].copy_from_slice(b"elr="); pos += 5;
            for i in (0..64).step_by(4).rev() {
                let d = ((ctx.elr_el1 >> i) & 0xf) as u8;
                buf[pos] = if d < 10 { b'0'+d } else { b'a'+d-10 }; pos += 1;
            }
            buf[pos] = b' '; pos += 1;
            buf[pos..pos+6].copy_from_slice(b"frst="); pos += 6;
            buf[pos] = b'0' + (ctx.first_run as u8); pos += 1;
            buf[pos] = b' '; pos += 1;
            buf[pos..pos+5].copy_from_slice(b"usr="); pos += 5;
            buf[pos] = b'0' + (ctx.is_user as u8); pos += 1;
            buf[pos] = b' '; pos += 1;
            buf[pos..pos+3].copy_from_slice(b"sp="); pos += 3;
            for i in (0..64).step_by(4).rev() {
                let d = ((ctx.sp >> i) & 0xf) as u8;
                buf[pos] = if d < 10 { b'0'+d } else { b'a'+d-10 }; pos += 1;
            }
        }
        
        // 下一个任务
        let next = &*next_ptr;
        buf[pos] = b' '; pos += 1;
        buf[pos..pos+4].copy_from_slice(b"nxt="); pos += 4;
        let pid = next.pid;
        if pid >= 10 { buf[pos] = b'0' + (pid/10) as u8; pos += 1; }
        buf[pos] = b'0' + (pid%10) as u8; pos += 1;
        buf[pos] = b' '; pos += 1;
        let nctx = &*next.context.get();
        buf[pos..pos+4].copy_from_slice(b"lr="); pos += 4;
        for i in (0..64).step_by(4).rev() {
            let d = ((nctx.lr >> i) & 0xf) as u8;
            buf[pos] = if d < 10 { b'0'+d } else { b'a'+d-10 }; pos += 1;
        }
        buf[pos] = b' '; pos += 1;
        buf[pos..pos+5].copy_from_slice(b"elr="); pos += 5;
        for i in (0..64).step_by(4).rev() {
            let d = ((nctx.elr_el1 >> i) & 0xf) as u8;
            buf[pos] = if d < 10 { b'0'+d } else { b'a'+d-10 }; pos += 1;
        }
        buf[pos] = b' '; pos += 1;
        buf[pos..pos+6].copy_from_slice(b"frst="); pos += 6;
        buf[pos] = b'0' + (nctx.first_run as u8); pos += 1;
        buf[pos] = b' '; pos += 1;
        buf[pos..pos+5].copy_from_slice(b"usr="); pos += 5;
        buf[pos] = b'0' + (nctx.is_user as u8); pos += 1;
        buf[pos] = b' '; pos += 1;
        buf[pos..pos+3].copy_from_slice(b"sp="); pos += 3;
        for i in (0..64).step_by(4).rev() {
            let d = ((nctx.sp >> i) & 0xf) as u8;
            buf[pos] = if d < 10 { b'0'+d } else { b'a'+d-10 }; pos += 1;
        }
        buf[pos] = b'\n'; pos += 1;
        
        ffi::c_print_str(buf.as_ptr());
    }
    }

    // 释放旧任务的 Arc（必须在 context_switch 之前，
    // 因为用户进程首次调度时 context_switch 会 eret 不返回）
    drop(cur_arc);

    // 上下文切换
    unsafe {
        let exit_addr = ffi::el1_exception_exit as *const () as u64;
        if (*next_ctx).lr == exit_addr {
            ffi::c_print_str(b"[SCHED] Fork child switch\n\0".as_ptr() as *const _);
        }
        context_switch(cur_ctx, next_ctx);
    }

    // ===== context_switch 返回后，已在新任务的栈上运行 =====
    // 只有内核线程能走到这里（用户进程首次调度走 eret 不返回）
}

/// 主动让出 CPU：将当前任务放回队列并调用 schedule。
/// 通过普通函数调用实现，后续 context_switch 返回时会回到调用者继续执行。
pub fn yield_now() {
    schedule();
}

/// 检查是否需要调度，若需要则主动让出。
/// 供线程在长时间循环中调用。
pub fn yield_if_need() {
    if NEED_RESCHED.load(Ordering::Relaxed) {
        schedule();
    }
}

/// 定时器中断回调：递减时间片，耗尽时标记需要重新调度。
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_timer_tick() {
    let cur_ptr = task::CURRENT_TASK.load(Ordering::Relaxed);
    if !cur_ptr.is_null() {
        let cur = unsafe { &mut *cur_ptr };
        cur.time_slice -= 1;
        if cur.time_slice <= 0 {
            // 时间片耗尽，重置并触发调度
            cur.time_slice = task::DEFAULT_TIME_SLICE;
            NEED_RESCHED.store(true, Ordering::Relaxed);
        }
    }
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
        }

        // 未发生切换，安全返回 C 层继续原有逻辑。
        0
    }
}
/// 打印就绪队列中所有任务的信息（用于调试）
fn dump_ready_queue() {
    let q_addr = &READY_QUEUE as *const _ as usize;
    let q = unsafe { &*READY_QUEUE.0.get() };
    println!("[RQ DUMP] --- ready queue @ {:#x} ---", q_addr);
    for prio in (0..task::PRIO_NR).rev() {
        let len = q[prio].len();
        if len == 0 { continue; }
        // 打印 VecDeque buf 地址（通过 len 和 cap 间接判断）
        let cap = q[prio].capacity();
        println!("[RQ] queue[{}] ({} tasks, cap={})", prio, len, cap);
        for task in q[prio].iter() {
            let ctx = unsafe { &*task.context.get() };
            let task_ptr = task.as_ref() as *const Task as usize;
            println!("[RQ]   pid={} task={:#x} sp={:#018x} lr={:#018x} elr={:#018x} name={}",
                task.pid, task_ptr, ctx.sp, ctx.lr, ctx.elr_el1, task.name);
        }
    }
    println!("[RQ DUMP] --- end ---");
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
    
    // println!("[kernel] Rust main starting...");



    unsafe {
      ffi::c_print_str(c"[RUST] All alloc tests passed!\n".as_ptr());
    }
    let idle = task::create_kernel_thread(idle_func, 0, "idle", 0);
    add_task(Arc::clone(&idle));
    // 泄漏初始 Arc，保证 idle 永远存活。
    core::mem::forget(idle);

    // 2. 启动 WaitQueue 闭环测试（定义在 wait.rs，无循环依赖）。
    // wait::start_wait_queue_test();  // 暂时关闭，避免干扰 TTY 测试

    // 3. 启动 virtio-blk 设备
    dump_ready_queue();
    crate::kernel::virtio::init();

    // 4. 挂载根文件系统（FAT32）
    {
        use crate::kernel::fs::fat32::Fat32FS;
        use crate::kernel::fs::vfs;
        use alloc::sync::Arc;

        // 从 FDT 解析 virtio block 设备的 MMIO 地址和 IRQ
        let mut mmio_pa = 0u64;
        let irq_num = unsafe { crate::ffi::rust_find_virtio_blk(&mut mmio_pa as *mut u64) };
        if irq_num == 0 || mmio_pa == 0 {
            println!("[VIRTIO] No virtio-blk device found in FDT");
        } else {
            println!("[VIRTIO] Found block device at {:#x}, IRQ {}", mmio_pa, irq_num);
        }
        match crate::kernel::virtio::VirtioBlk::new(mmio_pa, irq_num) {
            Ok(bdev) => {
                match Fat32FS::new(bdev as Arc<dyn vfs::BlockDevice>) {
                    Ok(fs) => {
                        vfs::mount("/", Arc::new(fs) as Arc<dyn vfs::FileSystem>);
                        println!("[FS] FAT32 root filesystem mounted");
                        if let Some(inode) = vfs::root_fs().map(|fs| fs.root_inode()) {
                            if let Ok(entries) = inode.readdir() {
                                for e in entries {
                                    let kind = if e.is_dir { "DIR " } else { "FILE" };
                                    println!("[FS]   {} {} ({} bytes)", kind, e.name, e.size);
                                }
                            }
                        }
                    }
                    Err(e) => println!("[FS] FAT32 mount failed: {}", e),
                }
            }
            Err(e) => println!("[FS] VirtioBlk init failed: {}", e),
        }
    }
    dump_ready_queue();

    // 5. 从 FS 加载 ELF 用户进程
    start_user_process();
    dump_ready_queue();

    // 3. 挑选第一个非 idle 任务并启动。
    // 因为 idle 已在队列中，从最高优先级队列出队一个任务，
    // 将 CURRENT_TASK 设为它，并直接恢复其上下文运行，永不返回。
    cpu::disable_irq();
    dump_ready_queue();

    let first_arc = {
        let q = unsafe { &mut *READY_QUEUE.0.get() };
        let mut found = None;
        for prio in (0..task::PRIO_NR).rev() {
            if let Some(task) = q[prio].pop_front() {
                found = Some(task);
                break;
            }
        }
        found.expect("rust_main: no task to start!")
    };
    let first_ptr =     Arc::into_raw(first_arc) as *mut Task;
    CURRENT_TASK.store(first_ptr, Ordering::Relaxed);

    // 切换到第一个任务的页表（用户进程需要自己的 TTBR0）
    unsafe {
        let first_pgd = (*first_ptr).pgd.as_pa();
        if first_pgd != 0 {
            pgtbl::switch_page_table(first_pgd);
        }
    }

    unsafe {
        let ctx_ptr = (*first_ptr).context.get();
        start_first_task(ctx_ptr);
    }
    
    loop {
        core::hint::spin_loop();
    }

}

use crate::kernel::pgtbl::PAGE_SIZE;
use crate::kernel::elf;
use crate::kernel::fs::vfs;
use crate::kernel::tty::TTY0;

/// 调试断点（如果不需要，可删除）
#[inline(never)]
pub fn debug_break() {
    #[cfg(target_arch = "x86_64")]
    unsafe {
        core::arch::asm!("int3");
    }
}

/// 启动第一个用户进程（shell）
///
/// # Panics
/// 如果读取 ELF 或加载失败，则 panic，因为这是系统启动的必要步骤。
pub fn start_user_process() {
    println!("[KERN] Loading user program from disk...");

    // 1. 读取 ELF，失败则直接崩溃（或根据需求改为 return）
    let elf_data = vfs::read_whole_file("/SHELL.ELF")
        .expect("[KERN] Failed to read SHELL.ELF");
    println!("[KERN] SHELL.ELF loaded ({} bytes)", elf_data.len());

    // 2. 加载 ELF
    let loaded = elf::load_elf(&elf_data)
        .expect("[KERN] ELF load failed");
    println!("[KERN] elf::load_elf OK");

    // 3. 设置 Linux AArch64 启动栈帧（argc, argv, envp, auxv）
    let init_sp = crate::kernel::elf::setup_user_stack(
        &loaded.stack_pas, loaded.stack_top,
        loaded.entry, loaded.phdr_va, loaded.phnum,
    );

    // 4. 创建任务
    let task_arc = task::create_user_process(
        loaded.entry as usize,
        init_sp,
        "shell",
        5,
        loaded.pgd,
        loaded.code_pa,
        loaded.code_va,
        loaded.stack_pa,
        loaded.stack_va,
        loaded.stack_pages,
    );

    // 4. 创建 VMA
    {
        let mm = task_arc
            .mm
            .as_ref()
            .expect("[KERN] Task's mm should be Some")
            .as_mut();
        
        let stack_bottom = (loaded.stack_top as u64)
            .saturating_sub((loaded.stack_pages as u64) * PAGE_SIZE as u64);

        mm.vma_create(
            loaded.code_va as u64,
            loaded.code_end as u64,
            crate::kernel::mm::VM_READ | crate::kernel::mm::VM_EXEC,
        ).expect("[KERN] VMA creation for code failed");

        mm.vma_create(
            stack_bottom,
            loaded.stack_top as u64,
            crate::kernel::mm::VM_READ | crate::kernel::mm::VM_WRITE,
        ).expect("[KERN] VMA creation for stack failed");

        println!(
            "[KERN] VMA: code [{:#x}..{:#x}] RX, stack [{:#x}..{:#x}] RW",
            loaded.code_va, loaded.code_end, stack_bottom, loaded.stack_top
        );

        if crate::config::MM_DEBUG {
            crate::kernel::task::dump_task_mm(&task_arc);
        }
    }

    // 5. 终端设置并加入调度队列
    let pid = task_arc.pid;
    TTY0.set_foreground(pid);
    if crate::config::MM_DEBUG {
        let pgd_pa = task_arc.pgd.as_pa();
        unsafe { crate::ffi::dump_page_table(pgd_pa); }
    }
    add_task(task_arc);

    println!("[KERN] User process started: pid={}", pid);
}

    // 1. idle 线程，永远循环，不会返回。
    fn idle_func(_: usize) {
        unsafe extern "C" {
            static system_tick: u64;
        }
        let mut last_print: u64 = 0;
        loop {
            let tick = unsafe { system_tick };
            // 每 10 秒打印一次（system_tick 每秒 +1000）
            if tick >= last_print + 10000 {
                // println!("[IDLE] alive, tick: {}", tick);
                last_print = tick;
            }
            // 低功耗等待中断，如 `wfi`
            unsafe { core::arch::asm!("wfi"); }
        }
    }