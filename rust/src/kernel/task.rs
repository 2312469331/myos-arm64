/// 任务数据结构与工厂，包含自引用初始化。
/// 仅依赖 arch 模块和 alloc。

use alloc::boxed::Box;
use alloc::string::String;
use alloc::sync::Arc;
use alloc::vec::Vec;
use core::cell::UnsafeCell;
use core::sync::atomic::{AtomicPtr, AtomicUsize, Ordering};

use crate::println;
use crate::kernel::pgtbl;
use crate::kernel::fs::vfs::FdTable;

pub const TASK_RUNNING: usize = 0;

// 简易 errno
const ECHILD: i64 = 10;
pub const TASK_SLEEPING: usize = 1;
pub const TASK_DEAD: usize = 2;
pub const TASK_ZOMBIE: usize = 3;

/// 默认时间片（tick 数，由 TIME_SLICE_TICKS 直接决定）
pub const DEFAULT_TIME_SLICE: i32 = crate::config::TIME_SLICE_TICKS;
/// 默认优先级（范围 0-9，数字越大优先级越高）
pub const DEFAULT_PRIO: usize = 5;
/// 优先级数量
pub const PRIO_NR: usize = 10;
/// 内核线程 PID 起始值（用户进程从 1 开始）
pub const KERNEL_PID_BASE: usize = 0;
use crate::arch::aarch64::context::{TaskContext, kthread_entry};
use crate::ffi;

/// 全局 PID 计数器
static NEXT_PID: AtomicUsize = AtomicUsize::new(1);

/// 分配一个新的 PID
pub fn alloc_pid() -> usize {
    NEXT_PID.fetch_add(1, Ordering::Relaxed)
}

/// mm_struct 所有权包装：Drop 时释放 Rust MmStruct（自动 drop BTreeMap）。
pub struct MmOwner {
    ptr: *mut crate::kernel::mm::MmStruct,
    owned: bool, // true = 拥有所有权，Drop 时释放
}

impl MmOwner {
    pub fn new(ptr: *mut crate::kernel::mm::MmStruct, owned: bool) -> Self {
        Self { ptr, owned }
    }
    pub fn as_ptr(&self) -> *mut crate::kernel::mm::MmStruct {
        self.ptr
    }
    pub fn as_mut(&self) -> &'static mut crate::kernel::mm::MmStruct {
        unsafe { &mut *self.ptr }
    }
}

impl Drop for MmOwner {
    fn drop(&mut self) {
        if !self.ptr.is_null() && self.owned {
            unsafe {
                let _ = Box::from_raw(self.ptr);
            }
        }
    }
}

/// 页表所有权包装：Drop 时递归释放整个 4 级页表树（L0→L1→L2→L3）。
/// 内核线程 pgd=0，用 PageTable::none() 表示。
pub struct PageTable(u64);

impl PageTable {
    pub fn new(l0_pa: u64) -> Self {
        Self(l0_pa)
    }
    pub fn none() -> Self {
        Self(0)
    }
    pub fn as_pa(&self) -> u64 {
        self.0
    }
    pub fn is_none(&self) -> bool {
        self.0 == 0
    }
    pub fn is_some(&self) -> bool {
        self.0 != 0
    }
}

impl Drop for PageTable {
    fn drop(&mut self) {
        if self.0 != 0 {
            unsafe { crate::ffi::free_page_table_tree(self.0); }
        }
    }
}

/// 进程控制块（PCB）。
/// 内核线程和用户进程共用同一结构。
pub struct Task {
    pub stack: Box<[u8; 32768]>,
    pub context: UnsafeCell<TaskContext>,
    /// 自引用：指向自身 Arc。
    pub arc_self: UnsafeCell<Option<Arc<Task>>>,
    pub entry: fn(usize),
    pub arg: usize,
    pub name: &'static str,
    pub state: AtomicUsize,
    /// 剩余时间片（tick 数），用完触发调度
    pub time_slice: i32,
    /// 优先级（0-9，数字越大优先级越高）
    pub prio: usize,
    // ---- 进程相关字段 ----
    /// 进程 ID
    pub pid: usize,
    /// 父进程 PID
    pub ppid: usize,
    /// 页表物理地址（PGD），内核线程为 PageTable::none()
    pub pgd: PageTable,
    /// 用户态栈指针
    pub user_sp: usize,
    /// 内核态栈指针（保存在 context.sp）
    /// 进程退出码（wait 用）
    pub exit_code: i32,
    /// 进程地址空间描述符。
    /// - Some(MmOwner)：拥有所有权，Drop 时释放 mm
    /// - None：fork 子进程，不拥有所有权，不释放
    pub mm: Option<MmOwner>,
    // ---- 用户进程资源（退出时释放） ----
    /// 代码页物理地址
    pub code_pa: u64,
    /// 代码页虚拟地址
    pub code_va: usize,
    /// 栈页物理地址
    pub stack_pa: u64,
    /// 栈顶虚拟地址
    pub stack_va: usize,
    /// 栈页数量
    pub stack_pages: u32,
    /// 信号状态
    pub signals: crate::kernel::signal::SignalState,
    /// 进程组 ID（用于作业控制）
    pub pgid: usize,
    /// 文件描述符表
    pub fd_table: FdTable,
    /// 当前工作目录
    pub cwd: String,
}

impl Drop for Task {
    fn drop(&mut self) {
        // 释放子进程独占的物理资源（代码页、栈页）
        // mm 由 MmOwner::drop 负责，页表由 PageTable::drop 负责
        if self.pgd.is_some() {
            if self.code_pa != 0 {
                unsafe { crate::ffi::free_phys_pages(self.code_pa, 0); }
            }
            if self.stack_pa != 0 && self.stack_pages > 0 {
                unsafe { crate::ffi::free_phys_pages(self.stack_pa, self.stack_pages.ilog2() as u32); }
            }
        }
        // PageTable::drop 自动释放页表树
        // Box<[u8; 16384]> (stack) 自动释放
    }
}

/// 获取当前任务的 mm 裸指针（供 FFI / C page_fault 调用）
pub fn current_mm_ptr() -> *mut crate::ffi::MmStruct {
    let ptr = CURRENT_TASK.load(Ordering::Relaxed);
    if ptr.is_null() {
        return core::ptr::null_mut();
    }
    let task = unsafe { &*ptr };
    match task.mm {
        Some(ref mm) => mm.as_ptr() as *mut crate::ffi::MmStruct,
        None => core::ptr::null_mut(),
    }
}

/// 获取当前任务的引用（供内部模块如 TTY 检查信号）
pub fn current_task_ref() -> Option<&'static Task> {
    let ptr = CURRENT_TASK.load(Ordering::Relaxed);
    if ptr.is_null() {
        None
    } else {
        Some(unsafe { &*ptr })
    }
}

/// 获取当前任务的 Rust MmStruct 引用
pub fn current_mm() -> Option<&'static mut crate::kernel::mm::MmStruct> {
    let ptr = CURRENT_TASK.load(Ordering::Relaxed);
    if ptr.is_null() {
        return None;
    }
    let task = unsafe { &*ptr };
    match task.mm {
        Some(ref mm) => Some(mm.as_mut()),
        None => None,
    }
}

/// 获取当前任务的 fd 表
pub fn current_fd_table() -> Option<&'static mut FdTable> {
    let ptr = CURRENT_TASK.load(Ordering::Relaxed);
    if ptr.is_null() { return None; }
    let task = unsafe { &mut *ptr };
    Some(&mut task.fd_table)
}

/// 获取当前工作目录
pub fn current_cwd() -> Option<String> {
    let ptr = CURRENT_TASK.load(Ordering::Relaxed);
    if ptr.is_null() { return None; }
    let task = unsafe { &*ptr };
    Some(task.cwd.clone())
}

/// 设置当前工作目录
pub fn set_cwd(path: &str) {
    let ptr = CURRENT_TASK.load(Ordering::Relaxed);
    if ptr.is_null() { return; }
    let task = unsafe { &mut *ptr };
    task.cwd = String::from(path);
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
    prio: usize,
) -> Arc<Task> {
    let stack = Box::new([0u8; 32768]);
    // ARM 栈向下增长，sp 指向数组末端 + 1 并对齐 16 字节
    let sp_base = stack.as_ptr() as *const u8;
    let sp = (sp_base as usize + 32768) & !15; // 16 字节对齐
    let mut ctx = TaskContext::zeroed();
    ctx.sp = sp as u64;
    ctx.lr = kthread_entry as *const () as u64;  // ✅ 先转指针，再转 u64
    ctx.elr_el1 = kthread_entry as *const () as u64;  // ✅ eret 跳转地址
    // 高优先级线程获得更长的时间片：base + prio * 2
    let time_slice = DEFAULT_TIME_SLICE + (prio as i32) * 2;
    let task = Task {
        stack,
        context: UnsafeCell::new(ctx),
        arc_self: UnsafeCell::new(None),
        entry,
        arg,
        name,
        state: AtomicUsize::new(TASK_RUNNING),
        time_slice,
        prio,
        pid: alloc_pid(),
        ppid: 0,
        pgd: PageTable::none(),
        user_sp: 0,
        exit_code: 0,
        mm: None,
        code_pa: 0,
        code_va: 0,
        stack_pa: 0,
        stack_va: 0,
        stack_pages: 0,
        signals: crate::kernel::signal::SignalState::new(),
        pgid: 0,
        fd_table: FdTable::new(),
        cwd: String::from("/"),
    };

    let arc = Arc::new(task);
    unsafe {
        (*arc.arc_self.get()) = Some(Arc::clone(&arc));
    }
    return arc;
}

/// 创建用户进程。
/// 使用 from_user_entry 创建 EL0 上下文，分配 mm_struct。
pub fn create_user_process(
    entry: usize,        // 用户态入口地址
    user_sp: usize,      // 用户态栈指针
    name: &'static str,
    prio: usize,
    pgd: u64,            // 页表物理地址
    code_pa: u64,        // 代码页物理地址
    code_va: usize,      // 代码页虚拟地址
    stack_pa: u64,       // 栈页物理地址
    stack_va: usize,     // 栈顶虚拟地址
    stack_pages: u32,    // 栈页数量
) -> Arc<Task> {
    let stack = Box::new([0u8; 32768]);
    let sp_base = stack.as_ptr() as *const u8;
    let sp = (sp_base as usize + 32768) & !15;
    let ctx = TaskContext::from_user_entry(entry as u64, user_sp as u64, sp as u64);
    let time_slice = DEFAULT_TIME_SLICE + (prio as i32) * 2;

    let mm = Box::into_raw(Box::new(crate::kernel::mm::MmStruct::new()));

    fn dummy_entry(_: usize) {}
    let task = Task {
        stack,
        context: UnsafeCell::new(ctx),
        arc_self: UnsafeCell::new(None),
        entry: dummy_entry,
        arg: 0,
        name,
        state: AtomicUsize::new(TASK_RUNNING),
        time_slice,
        prio,
        pid: alloc_pid(),
        ppid: 0,
        pgd: PageTable::new(pgd),
        user_sp: user_sp as usize,
        exit_code: 0,
        mm: Some(MmOwner::new(mm, true)),
        code_pa,
        code_va,
        stack_pa,
        stack_va,
        stack_pages,
        signals: crate::kernel::signal::SignalState::new(),
        pgid: 0,
        fd_table: FdTable::new(),
        cwd: String::from("/"),
    };

    let arc = Arc::new(task);
    unsafe {
        (*arc.arc_self.get()) = Some(Arc::clone(&arc));
    }
    return arc;
}

/// 创建用户进程。

/// 被 `kthread_entry` 调用的包装器。
/// 从 CURRENT_TASK 读取入口并执行。
/// 线程函数返回后，标记 TASK_DEAD 并调度，不再死循环。
#[unsafe(no_mangle)]
pub extern "C" fn kthread_wrapper() {
    let task_ptr = CURRENT_TASK.load(Ordering::Relaxed);
    if task_ptr.is_null() {
        loop {}
    }
    let task = unsafe { &*task_ptr };
    let entry = task.entry;
    let arg = task.arg;
    entry(arg);

    // 线程函数返回，执行退出清理
    do_exit();
    // 不会执行到这里
    loop {}
}

/// 进程退出：存退出码、变僵尸、通知父进程
pub fn do_exit_with(code: i32) {
    let task_ptr = CURRENT_TASK.load(Ordering::Relaxed);
    if task_ptr.is_null() {
        return;
    }
    let task = unsafe { &mut *task_ptr };
    task.exit_code = code;

    // 切回内核页表
    if task.pgd.is_some() {
        pgtbl::switch_page_table(0);
    }

    // 标记 ZOMBIE，保留 PCB 等父进程 wait 回收
    // arc_self 不释放，父进程还能读到 exit_code
    task.state.store(TASK_ZOMBIE, Ordering::Relaxed);

    // 发送 SIGCHLD 给父进程
    let ppid = task.ppid;
    if ppid != 0 {
        do_kill(ppid, crate::kernel::signal::SIGCHLD);
    }

    crate::kernel::scheduler::schedule();
}

/// 无参数版本（被 C FFI 或 kthread 调用）
pub fn do_exit() {
    do_exit_with(0);
}

/// wait4：等待子进程退出
/// pid: -1=任意子进程, >0=指定 PID
/// wstatus: 写入退出状态码
pub fn do_wait4(pid: i64, wstatus: *mut i32) -> i64 {
    let cur_ptr = CURRENT_TASK.load(Ordering::Relaxed);
    if cur_ptr.is_null() { return neg_errno(ECHILD); }
    let cur = unsafe { &*cur_ptr };
    let cur_pid = cur.pid;

    // 找僵尸子进程
    use crate::kernel::scheduler;
    if let Some((code, child_pid)) = scheduler::find_zombie_child(cur_pid, pid) {
        if !wstatus.is_null() {
            unsafe { core::ptr::write(wstatus, code); }
        }
        return child_pid as i64;
    }

    // 检查是否还有活着的子进程
    let has_alive = {
        let p = scheduler::ALL_TASKS.lock_and_get();
        let r = unsafe { &*p }.iter().any(|t|
            t.ppid == cur_pid && t.state.load(Ordering::Relaxed) != TASK_DEAD && t.state.load(Ordering::Relaxed) != TASK_ZOMBIE);
        scheduler::ALL_TASKS.unlock();
        r
    };
    if has_alive {
        crate::arch::aarch64::cpu::NEED_RESCHED.store(true, Ordering::Relaxed);
        0
    } else {
        neg_errno(ECHILD)
    }
}

fn neg_errno(e: i64) -> i64 { (e as isize) as i64 }

/// C FFI：检查当前任务是否已被标记为 DEAD，如是则直接退出（供 page_fault handler 直接调用）
#[unsafe(no_mangle)]
pub extern "C" fn rust_do_exit_if_dead() {
    let ptr = CURRENT_TASK.load(Ordering::Relaxed);
    if ptr.is_null() {
        return;
    }
    let task = unsafe { &*ptr };
    if task.state.load(Ordering::Relaxed) == TASK_DEAD {
        do_exit();
    }
}

/// C FFI：进程退出（供信号处理调用）
#[unsafe(no_mangle)]
pub extern "C" fn rust_do_exit() {
    do_exit();
}

/// 诊断：验证父进程和子进程的栈区域页表（L1/L2/L3）
unsafe fn dump_stack_pgtbl(pgd: u64, label: &str) {
    unsafe {
        let l0 = pgtbl::phys_to_virt(pgd) as *const u64;
        let l0e = *l0.add(0);
        let l1_pa = (l0e & 0x0000FFFFFFFFF000) as u64;
        if l1_pa == 0 { return; }
        let l1 = pgtbl::phys_to_virt(l1_pa) as *const u64;
        for li in [1usize, 2] {
            let l1e = *l1.add(li);
            if (l1e & 1) == 0 { continue; }
            let l2_pa = (l1e & 0x0000FFFFFFFFF000) as u64;
            let l2 = pgtbl::phys_to_virt(l2_pa) as *const u64;
            println!("[FORK] {} L1[{}] L2 table @ {:#x}:", label, li, l2_pa);
            for l2i in 508..512 {
                let l2e = *l2.add(l2i);
                if (l2e & 1) == 0 { continue; }
                let l3_pa = (l2e & 0x0000FFFFFFFFF000) as u64;
                let l3 = pgtbl::phys_to_virt(l3_pa) as *const u64;
                println!("[FORK]   {} L1[{}] L2[{}] L3 table @ {:#x}:", label, li, l2i, l3_pa);
                for l3i in 0..512 {
                    let entry = *l3.add(l3i);
                    if (entry & 1) != 0 {
                        let va_base = ((li as u64) << 30) | ((l2i as u64) << 21) | ((l3i as u64) << 12);
                        println!("[FORK]     L3[{}] va={:#010x} pte={:#018x}", l3i, va_base, entry);
                    }
                }
            }
        }
    }
}

/// 诊断：统一打印任务的 mm 信息（pid, name, mm ptr, vmas 原始状态）
pub fn dump_task_mm(task: &Task) {
    if crate::config::MM_DEBUG {
        println!("[TASK] pid={} name={}", task.pid, task.name);
        match task.mm {
            Some(ref mm) => {
                let mm_ptr = mm.as_ptr();
                println!("[TASK]   mm: ptr={:#x} owned={}", mm_ptr as usize, mm.owned);
                let mm_ref = mm.as_mut();
                println!("[TASK]   brk={:#x} start_stack={:#x}", mm_ref.brk, mm_ref.start_stack);
                // 直接裸读 BTreeMap 内部布局: [root: *mut Node, height: usize, length: usize]
                unsafe {
                    let btree_ptr = &mm_ref.vmas as *const _ as *const usize;
                    let root_ptr = *btree_ptr;
                    let height = *btree_ptr.add(1);
                    let len_val = *btree_ptr.add(2);
                    println!("[TASK]   vmas: {} entries, root={:#x} height={}", len_val, root_ptr, height);
                    if root_ptr == 0 && len_val > 0 {
                        println!("[TASK]   *** BUG: root is NULL but len>0! BTreeMap corrupted!");
                    }
                }
            }
            None => {
                println!("[TASK]   mm: None");
            }
        }
    }
}

/// fork：复制当前用户进程
/// 返回子进程 PID（给父进程），子进程得到 0
pub fn do_fork(parent_regs: *const ffi::PtRegs) -> usize {
    
    use crate::kernel::scheduler;

    let cur_ptr = CURRENT_TASK.load(Ordering::Relaxed);
    if crate::config::FORK_DEBUG && !cur_ptr.is_null() {
        let cur_pgd = unsafe { (*cur_ptr).pgd.as_pa() };
        if cur_pgd != 0 {
            unsafe { crate::ffi::dump_page_table(cur_pgd); }
        }
    }
    if cur_ptr.is_null() {
        println!("[FORK] cur_ptr is null!");
        return 0;
    }
    let parent = unsafe { &*cur_ptr };

    // dump_task_mm(parent);

    // 诊断：打印 parent.mm 状态
    if crate::config::FORK_DEBUG {
        if let Some(ref parent_mm) = parent.mm {
            unsafe { dump_task_mm(parent); }
        }
    }

    if crate::config::FORK_DEBUG {
        println!("[FORK] parent pid={}, pgd={:#x}, code_pa={:#x}, stack_pa={:#x}",
                 parent.pid, parent.pgd.as_pa(), parent.code_pa, parent.stack_pa);
    }

    if parent.pgd.is_none() {
        if crate::config::FORK_DEBUG { println!("[FORK] parent.pgd == 0, not a user process"); }
        return 0;
    }

    let parent_pgd = parent.pgd.as_pa();
    let code_va = parent.code_va;
    let stack_va = parent.stack_va;
    let stack_pages = parent.stack_pages as usize;
    if crate::config::FORK_DEBUG {
        println!("[FORK] parent_pgd={:#x} code_va={:#x} stack_va={:#x} stack_pages={}",
            parent_pgd, code_va, stack_va, stack_pages);
    }

    // 1. 创建新的页表
    if crate::config::FORK_DEBUG { println!("[FORK] STEP 1: creating child PGD..."); }
    let child_pgd = match pgtbl::create_user_pgd() {
        Some(pgd) => {
            if crate::config::FORK_DEBUG { println!("[FORK] child_pgd={:#x}", pgd); }
            pgd
        }
        None => {
            println!("[FORK] Failed to create child page table");
            return 0;
        }
    };

    // 2. 复制整个用户态页表树（共享物理页，后续 COW 时再分离）
    if crate::config::FORK_DEBUG {
        println!("[FORK] STEP 2: copying page table...");

        // 诊断：打印父进程 L0 页表条目
        unsafe {
            let src_l0 = pgtbl::phys_to_virt(parent_pgd as u64) as *const u64;
            println!("[FORK] parent L0 page table @ phys={:#x} virt={:#p}:", parent_pgd, src_l0);
            for i in 0..4 {
                let entry = *src_l0.add(i);
                let phys = entry & 0x0000FFFFFFFFF000;
                let valid = (entry & 1) != 0;
                if valid {
                    println!("[FORK]   L0[{}]={:#018x} phys={:#x} VALID", i, entry, phys);
                }
            }
        }
    }

    if crate::config::FORK_DEBUG {
        unsafe { crate::ffi::dump_page_table(parent_pgd); }
    }
    unsafe {
        ffi::copy_user_page_table(parent_pgd, child_pgd);
    }
    // dump 子进程页表确认拷贝结果
    if crate::config::FORK_DEBUG {
        unsafe { crate::ffi::dump_page_table(child_pgd); }
    }
    if crate::config::FORK_DEBUG { println!("[FORK] STEP 2 done"); }

    if crate::config::FORK_DEBUG {
        // 诊断：验证子进程页表
        unsafe {
            let child_l0 = pgtbl::phys_to_virt(child_pgd as u64) as *const u64;
            println!("[FORK] child L0 page table @ phys={:#x}:", child_pgd);
            for i in 0..4 {
                let entry = *child_l0.add(i);
                let valid = (entry & 1) != 0;
                if valid {
                    let phys = entry & 0x0000FFFFFFFFF000;
                    println!("[FORK]   dst L0[{}]={:#018x} phys={:#x} VALID", i, entry, phys);
                }
            }
        }

        // 诊断：验证父进程和子进程的栈区域页表（L1/L2/L3）
        unsafe { dump_stack_pgtbl(parent_pgd as u64, "PARENT"); }
        unsafe { dump_stack_pgtbl(child_pgd as u64, "CHILD"); }
    }

    // 3. 分配子进程内核栈，在栈顶放置 pt_regs
    // 不要用 Box::new([0u8; N])——会在当前栈上放一个 N 字节临时数组！
    // 直接用堆分配器分配零化内存，避免栈爆炸
    if crate::config::FORK_DEBUG { println!("[FORK] STEP 3: allocating kernel stack..."); }
    let kernel_stack: Box<[u8; 32768]> = unsafe {
        let layout = core::alloc::Layout::from_size_align(32768, 16).unwrap();
        let ptr = alloc::alloc::alloc_zeroed(layout);
        if ptr.is_null() {
            panic!("[FORK] alloc_zeroed failed for kernel stack");
        }
        Box::from_raw(ptr as *mut [u8; 32768])
    };
    let stack_base = kernel_stack.as_ptr() as usize;
    let pt_regs_addr = stack_base + 32768 - ffi::PtRegs::SIZE;
    if crate::config::FORK_DEBUG { println!("[FORK] kernel_stack={:#x} pt_regs_addr={:#x}", stack_base, pt_regs_addr); }

    // 4. 复制父进程 pt_regs，子进程 x0 = 0
    unsafe {
        core::ptr::copy_nonoverlapping(parent_regs as *const u8, pt_regs_addr as *mut u8, ffi::PtRegs::SIZE);
        let pt = pt_regs_addr as *mut ffi::PtRegs;
        (*pt).x0 = 0;
    }

    // 5. 创建 TaskContext：lr = el1_exception_exit, sp = pt_regs
    // 从硬件寄存器读取当前用户栈指针（parent.user_sp 是创建时的值，可能已过时）
    let user_sp: u64;
    unsafe { core::arch::asm!("mrs {}, sp_el0", out(reg) user_sp); }
    let exit_addr = unsafe { ffi::el1_exception_exit as *const () as u64 };
    let ctx = TaskContext {
        x19: 0, x20: 0, x21: 0, x22: 0,
        x23: 0, x24: 0, x25: 0, x26: 0,
        x27: 0, x28: 0, x29: 0,
        lr: exit_addr,
        sp: pt_regs_addr as u64,
        sp_el0: user_sp,
        elr_el1: 0,
        spsr_el1: 0,
        first_run: 0,
        is_user: 1,
    };

    // 6. 创建子进程 Task

    // 深复制父进程 mm_struct（BTreeMap VMA 副本，页表由 copy_user_page_table 处理）
    let child_mm = if let Some(ref parent_mm) = parent.mm {
        if crate::config::FORK_DEBUG {
            println!("[FORK] parent.mm is Some, ptr={:#x}", parent_mm.as_ptr() as usize);
            println!("[FORK] calling MmStruct::copy_from...");
        }
        let parent_ref = parent_mm.as_mut();
        dump_task_mm(parent);
        let child_ptr = Box::into_raw(Box::new(crate::kernel::mm::MmStruct::copy_from(parent_ref)));
        if crate::config::FORK_DEBUG {
            println!("[FORK] MmStruct::copy_from done, child_ptr={:#x}", child_ptr as usize);
        }
        Some(MmOwner::new(child_ptr, true))
    } else {
        if crate::config::FORK_DEBUG { println!("[FORK] parent.mm is None"); }
        None
    };
    if crate::config::FORK_DEBUG { println!("[FORK] child_mm set"); }

    // 共享父进程的物理页（由页表复制实现），code_pa/stack_pa 置 0 防止 Task::drop 重复释放
    fn dummy_entry(_: usize) {}
    let time_slice = DEFAULT_TIME_SLICE + (parent.prio as i32) * 2;
    let mut task = Task {
        stack: kernel_stack,
        context: UnsafeCell::new(ctx),
        arc_self: UnsafeCell::new(None),
        entry: dummy_entry,
        arg: 0,
        name: "child",
        state: AtomicUsize::new(TASK_RUNNING),
        time_slice,
        prio: parent.prio,
        pid: alloc_pid(),
        ppid: parent.pid,
        pgd: PageTable::new(child_pgd),
        user_sp: user_sp as usize,
        exit_code: 0,
        mm: child_mm,
        code_pa: 0,           // 共享父进程物理页，不拥有所有权
        code_va: code_va,
        stack_pa: 0,          // 共享父进程物理页，不拥有所有权
        stack_va: stack_va,
        stack_pages: stack_pages as u32,
        signals: crate::kernel::signal::SignalState::new(),
        pgid: parent.pgid,
        fd_table: {
            let mut child_fds = FdTable::new();
            child_fds.clone_from(&parent.fd_table);
            child_fds
        },
        cwd: parent.cwd.clone(),
    };

    let arc = Arc::new(task);
    unsafe {
        (*arc.arc_self.get()) = Some(Arc::clone(&arc));
    }

    let pid = unsafe { (*arc.arc_self.get()).as_ref().unwrap().pid };

    if crate::config::FORK_DEBUG {
        println!("[FORK] Created child pid={}, ppid={}, elr={:#x}, sp={:#x}",
                 pid, parent.pid, unsafe { (*parent_regs).elr_el1 }, pt_regs_addr);
    }

    scheduler::add_task(arc);

    pid
}

/// wait：等待子进程退出
pub fn do_wait() {
    let cur_ptr = CURRENT_TASK.load(Ordering::Relaxed);
    if cur_ptr.is_null() {
        return;
    }
    let task = unsafe { &mut *cur_ptr };

    // 找不到僵尸子进程就主动让出 CPU，等下次调度再检查
    crate::arch::aarch64::cpu::NEED_RESCHED.store(true, Ordering::Relaxed);
}

#[unsafe(no_mangle)]
pub extern "C" fn get_current_task_info(pid_out: *mut usize, ppid_out: *mut usize, pgd_out: *mut usize, name_out: *mut *const u8) {
    let ptr = CURRENT_TASK.load(Ordering::Relaxed);
    if ptr.is_null() {
        unsafe {
            *pid_out = 0;
            *ppid_out = 0;
            *pgd_out = 0;
            *name_out = core::ptr::null();
        }
        return;
    }
    let task = unsafe { &*ptr };
    unsafe {
        *pid_out = task.pid;
        *ppid_out = task.ppid;
        *pgd_out = task.pgd.as_pa() as usize;
        *name_out = task.name.as_ptr();
    }
}

/// FFI：返回当前进程 PID（供 page_fault.c 在异常上下文中调用）
#[unsafe(no_mangle)]
pub extern "C" fn rust_get_current_pid() -> i32 {
    let ptr = CURRENT_TASK.load(Ordering::Relaxed);
    if ptr.is_null() {
        return 0;
    }
    unsafe { (*ptr).pid as i32 }
}

/// FFI：向当前进程发送 SIGKILL（供 page_fault.c 在异常上下文中调用）
#[unsafe(no_mangle)]
pub extern "C" fn rust_send_sigkill_current() {
    let ptr = CURRENT_TASK.load(Ordering::Relaxed);
    if ptr.is_null() {
        return;
    }
    let pid = unsafe { (*ptr).pid };
    do_kill(pid, 9); /* SIGKILL */
}

/// FFI：向当前进程发送 SIGSEGV（供 page_fault.c 在异常上下文中调用）
#[unsafe(no_mangle)]
pub extern "C" fn rust_send_sigsegv_current() {
    let ptr = CURRENT_TASK.load(Ordering::Relaxed);
    if ptr.is_null() {
        return;
    }
    let pid = unsafe { (*ptr).pid };
    do_kill(pid, 11); /* SIGSEGV */
}

/// kill：向目标进程发送信号
pub fn do_kill(pid: usize, sig: usize) -> usize {
    use crate::kernel::signal::{self, SigAction};

    if sig >= signal::MAX_SIGNALS {
        return usize::MAX; // EINVAL
    }

    // pid=0: 发给当前进程组所有进程（暂不支持）
    // pid=-1: 发给所有进程（暂不支持）
    if pid == 0 || pid == usize::MAX {
        println!("[KILL] pid={} not supported yet", pid);
        return usize::MAX;
    }

    let target = match crate::kernel::scheduler::find_task_by_pid(pid) {
        Some(t) => t,
        None => {
            println!("[KILL] pid={} not found", pid);
            return usize::MAX; // ESRCH
        }
    };

    // 检查权限（暂不检查，内核态直接允许）

    // SIGKILL 直接终止
    if sig == signal::SIGKILL {
        println!("[KILL] sending SIGKILL to pid={}", pid);
        target.state.store(TASK_DEAD, Ordering::Relaxed);
        target.signals.send(sig);
        return 0;
    }

    // SIGSTOP 直接暂停
    if sig == signal::SIGSTOP {
        target.state.store(TASK_SLEEPING, Ordering::Relaxed);
        return 0;
    }

    // SIGCONT 直接继续
    if sig == signal::SIGCONT {
        target.state.store(TASK_RUNNING, Ordering::Relaxed);
        return 0;
    }

    // 其他信号：设为 pending
    target.signals.send(sig);
    println!("[KILL] signal {} pending for pid={}", sig, pid);
    0
}

/// C FFI：检查当前进程是否有待处理信号并执行默认动作
/// 返回 1 = 进程被终止，0 = 正常
#[unsafe(no_mangle)]
pub extern "C" fn rust_check_signals() -> i32 {
    let ptr = CURRENT_TASK.load(Ordering::Relaxed);
    if ptr.is_null() {
        return 0;
    }
    let task = unsafe { &*ptr };

    let sig = task.signals.check_pending();
    if sig == 0 {
        return 0;
    }

    use crate::kernel::signal::{self, SigAction};

    let action = signal::default_action(sig);
    match action {
        SigAction::Terminate => {
            println!("[SIGNAL] pid={} terminated by signal {}", task.pid, sig);
            task.state.store(TASK_DEAD, Ordering::Relaxed);
            task.signals.clear_all_pending();
            1
        }
        SigAction::Core => {
            println!("[SIGNAL] pid={} killed by signal {} (core dump)", task.pid, sig);
            task.state.store(TASK_DEAD, Ordering::Relaxed);
            task.signals.clear_all_pending();
            1
        }
        SigAction::Ignore => {
            task.signals.clear_pending(sig);
            0
        }
        SigAction::Stop => {
            println!("[SIGNAL] pid={} stopped by signal {}", task.pid, sig);
            task.state.store(TASK_SLEEPING, Ordering::Relaxed);
            task.signals.clear_pending(sig);
            0
        }
        SigAction::Cont => {
            task.state.store(TASK_RUNNING, Ordering::Relaxed);
            task.signals.clear_pending(sig);
            0
        }
        SigAction::Handler => {
            // TODO: 跳转到用户态 handler
            task.signals.clear_pending(sig);
            0
        }
    }
}