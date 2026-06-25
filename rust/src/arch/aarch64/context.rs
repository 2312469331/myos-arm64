/// 纯内联汇编层：任务上下文结构、切换、入口、首个任务启动。
/// 使用 `#[unsafe(naked)]` 裸函数实现，符合 stable-1.95。

// 普通函数用 asm!，naked 函数用 naked_asm!
// 这里我们不需要 use core::arch::asm;（因为 naked 不用它）

/// 任务上下文，保存被调用者需保存的寄存器。
/// 大小 17 × 8 = 136 字节，精确对应汇编中的存取顺序。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct TaskContext {
    // ---- 基础上下文（EL1 调用者保存） ----
    pub x19: u64,
    pub x20: u64,
    pub x21: u64,
    pub x22: u64,
    pub x23: u64,
    pub x24: u64,
    pub x25: u64,
    pub x26: u64,
    pub x27: u64,
    pub x28: u64,
    pub x29: u64,
    pub lr: u64,   // x30 返回地址
    pub sp: u64,
    // ---- EL0 切换上下文 ----
    pub sp_el0: u64,   // 用户态栈指针
    pub elr_el1: u64,  // 异常返回地址
    pub spsr_el1: u64, // 处理器状态
    // ---- 调度标志 ----
    pub first_run: u64, // 1=首次调度（需手动 eret），0=已调度过（用 ret）
    pub is_user: u64,   // 0=内核线程（ret），1=用户进程（eret）
}

impl TaskContext {
    pub const fn zeroed() -> Self {
        TaskContext {
            x19: 0, x20: 0, x21: 0, x22: 0,
            x23: 0, x24: 0, x25: 0, x26: 0,
            x27: 0, x28: 0, x29: 0, lr: 0, sp: 0,
            sp_el0: 0, elr_el1: 0, spsr_el1: 0,
            first_run: 0, is_user: 0,
        }
    }

    /// 创建用于跳转到 EL0 用户态的上下文
    /// user_entry: 用户态入口地址
    /// user_sp: 用户态栈指针
    /// kernel_sp: 内核栈指针（SVC 陷入内核时使用）
    pub fn from_user_entry(user_entry: u64, user_sp: u64, kernel_sp: u64) -> Self {
        TaskContext {
            x19: 0, x20: 0, x21: 0, x22: 0,
            x23: 0, x24: 0, x25: 0, x26: 0,
            x27: 0, x28: 0, x29: 0,
            lr: user_entry,  // ERET 后跳转到用户入口
            sp: kernel_sp,   // 内核栈：SVC 陷入时 CPU 自动用这个做栈
            sp_el0: user_sp, // 用户态栈
            elr_el1: user_entry, // 返回地址
            // EL0t 模式：M[3:0]=0000, 不屏蔽任何中断
            spsr_el1: 0x000,
            first_run: 1,    // 首次调度，需手动 eret
            is_user: 1,      // 标记为用户进程，使用 eret
        }
    }
}


/// 保存当前上下文到 `current`，从 `next` 恢复上下文并返回。
/// # Safety
/// 裸函数，调用者保证两个指针均有效。
#[unsafe(naked)]
pub unsafe extern "C" fn context_switch(
    _current: *mut TaskContext,
    _next: *const TaskContext,
) {
    core::arch::naked_asm!(
        "mov x8, x0",
        "mov x9, x1",
        // 保存当前 EL1 调用者寄存器
        "stp x19, x20, [x8], #16",
        "stp x21, x22, [x8], #16",
        "stp x23, x24, [x8], #16",
        "stp x25, x26, [x8], #16",
        "stp x27, x28, [x8], #16",
        "stp x29, x30, [x8], #16",
        "mov x10, sp",
        "str x10, [x8], #8",
        // 保存 EL0 相关寄存器
        "mrs     x10, sp_el0",
        "str     x10, [x8], #8",
        "mrs     x10, elr_el1",
        "str     x10, [x8], #8",
        "mrs     x10, spsr_el1",
        "str     x10, [x8], #8",

        // ========== 2. 从 next(x9) 恢复 新任务上下文 ==========
        // 始终恢复用户栈指针 sp_el0（所有用户任务都需要正确的栈指针）
        "ldr     x10, [x9, #104]",  // sp_el0
        "msr     sp_el0, x10",
        "mov     x12, x10",          // x12 = sp_el0（x10 后面会被覆盖）

        // 检查 first_run：1=首次（需 eret 进 EL0），0=非首次（正常 ret）
        "ldr     x11, [x9, #128]",  // first_run
        "cbz     x11, 2f",           // first_run=0 → 非首次，正常 ret
        "ldr     x11, [x9, #136]",  // is_user
        "cbz     x11, 2f",           // 内核线程 → 正常 ret

        // 用户进程首次调度：
        // 设置 elr_el1/spsr_el1（sp_el0 已在上面恢复），然后 eret 进用户态
        "ldr     x10, [x9, #112]",  // elr_el1
        "msr     elr_el1, x10",
        "ldr     x10, [x9, #120]",  // spsr_el1
        "msr     spsr_el1, x10",

        // 恢复通用寄存器 + eret
        "ldp x19, x20, [x9], #16",
        "ldp x21, x22, [x9], #16",
        "ldp x23, x24, [x9], #16",
        "ldp x25, x26, [x9], #16",
        "ldp x27, x28, [x9], #16",
        "ldp x29, x30, [x9], #16",
        "ldr x10, [x9], #8",
        "mov sp, x10",
        "mov x11, #0",
        "str x11, [x9, #24]",  // first_run = 0（x9 已自增到 offset 104，first_run 在 128）
        // 设置 TPIDR_EL0 = sp_el0（线程指针，glibc TLS 用）
        "msr     tpidr_el0, x12",
        // 设置 x0/x1/x2/x3（x12 保存了 sp_el0，x10 已被 SP 覆盖）
        "mov     x0, xzr",
        "add     x1, x12, #8",
        "add     x2, x12, #24",
        "add     x3, x12, #32",
        "eret",

        // 正常路径：
        // elr_el1/spsr_el1 保留在 pt_regs 中由 el1_exception_exit 恢复。
        // 只恢复 EL1 调用者寄存器，然后 ret 返回。
        // sp_el0 已在路径开头恢复完毕。
        "2:",
        "ldp x19, x20, [x9], #16",
        "ldp x21, x22, [x9], #16",
        "ldp x23, x24, [x9], #16",
        "ldp x25, x26, [x9], #16",
        "ldp x27, x28, [x9], #16",
        "ldp x29, x30, [x9], #16",
        "ldr x10, [x9], #8",
        "mov sp, x10",
        "ret",
    )
}

// /// 内核线程首次入口点：开中断并调用 `kthread_wrapper`。
// #[unsafe(naked)]
// pub unsafe extern "C" fn kthread_entry() -> ! {
//     // 🔥 NAKED 函数必须用 NAKED_ASM!
//     core::arch::naked_asm!(
//         "msr daifclr, #2",
//         "bl {wrapper}",
//         "udf #0",
//         wrapper = sym crate::kernel::task::kthread_wrapper,
//         options(noreturn),
//     )
// }
/// 内核线程首次入口点：开中断并调用 `kthread_wrapper`。
/// 不传参，通过全局 CURRENT_TASK 获取任务信息。
#[unsafe(naked)]
pub unsafe extern "C" fn kthread_entry() -> ! {
    // 🔥 修复：去掉 noreturn，去掉 sym，直接写函数名
    core::arch::naked_asm!(
        "msr daifclr, #2",
        "bl kthread_wrapper",    // 直接写汇编符号
        "udf #0",
        // 🔥 禁止：options(noreturn) 和 sym
    )
}


/// 启动第一个任务（普通函数，可用 asm!）
/// 区分内核线程（is_user==0）和用户进程（is_user==1）
/// 内核线程用 ret，用户进程用 eret 切换到 EL0
#[inline]
pub fn start_first_task(next: *const TaskContext) -> ! {
    unsafe {
        core::arch::asm!(
            // 恢复 EL0 相关寄存器（用户进程需要）
            "ldr     x10, [x0, #104]",  // sp_el0
            "msr     sp_el0, x10",
            "mov     x12, x10",          // x12 = sp_el0（x10 后面会被覆盖）
            "ldr     x10, [x0, #112]",  // elr_el1
            "msr     elr_el1, x10",
            "ldr     x10, [x0, #120]",  // spsr_el1
            "msr     spsr_el1, x10",
            "ldr     x11, [x0, #136]",  // is_user（提前加载，后面 x0 会被 ldp 推进）
            // 恢复 EL1 调用者寄存器
            "ldp x19, x20, [x0], #16",
            "ldp x21, x22, [x0], #16",
            "ldp x23, x24, [x0], #16",
            "ldp x25, x26, [x0], #16",
            "ldp x27, x28, [x0], #16",
            "ldp x29, x30, [x0], #16",
            "ldr x10, [x0], #8",
            "mov sp, x10",
            // 判断：is_user != 0 → 用户进程，用 eret
            "cbz     x11, 1f",
            // 清除 first_run（x0 已自增到 offset 104，first_run 在 128）
            "str     xzr, [x0, #24]",
            // 设置 TPIDR_EL0 = sp_el0（线程指针，glibc TLS 用，指向栈防止非法访问）
            "msr     tpidr_el0, x12",
            // 设置 x0=argc x1=argv x2=envp x3=auxv（x12 保存了 sp_el0）
            "mov     x0, xzr",
            "add     x1, x12, #8",
            "add     x2, x12, #24",
            "add     x3, x12, #32",
            "eret",
            "1:",
            "ret",                           // 内核线程
            in("x0") next,
            options(noreturn),
        )
    }
}