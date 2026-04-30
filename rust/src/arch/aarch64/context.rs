/// 纯内联汇编层：任务上下文结构、切换、入口、首个任务启动。
/// 使用 `#[unsafe(naked)]` 裸函数实现，符合 stable-1.95。

// 普通函数用 asm!，naked 函数用 naked_asm!
// 这里我们不需要 use core::arch::asm;（因为 naked 不用它）

/// 任务上下文，保存被调用者需保存的寄存器。
/// 大小 13 × 8 = 104 字节，精确对应汇编中的存取顺序。
#[repr(C)]
#[derive(Clone, Copy)]
pub struct TaskContext {
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
}

impl TaskContext {
    pub const fn zeroed() -> Self {
        TaskContext {
            x19: 0, x20: 0, x21: 0, x22: 0,
            x23: 0, x24: 0, x25: 0, x26: 0,
            x27: 0, x28: 0, x29: 0, lr: 0, sp: 0,
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
    // 🔥 NAKED 函数必须用 NAKED_ASM!
    core::arch::naked_asm!(
        "mov x8, x0",
        "mov x9, x1",
        // 保存当前
        "stp x19, x20, [x8], #16",
        "stp x21, x22, [x8], #16",
        "stp x23, x24, [x8], #16",
        "stp x25, x26, [x8], #16",
        "stp x27, x28, [x8], #16",
        "stp x29, x30, [x8], #16",
        "mov x10, sp",
        "str x10, [x8]",
        // 加载下一个
        "ldp x19, x20, [x9], #16",
        "ldp x21, x22, [x9], #16",
        "ldp x23, x24, [x9], #16",
        "ldp x25, x26, [x9], #16",
        "ldp x27, x28, [x9], #16",
        "ldp x29, x30, [x9], #16",
        "ldr x10, [x9]",
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
#[inline]
pub fn start_first_task(next: *const TaskContext) -> ! {
    unsafe {
        core::arch::asm!(
            "ldp x19, x20, [x0], #16",
            "ldp x21, x22, [x0], #16",
            "ldp x23, x24, [x0], #16",
            "ldp x25, x26, [x0], #16",
            "ldp x27, x28, [x0], #16",
            "ldp x29, x30, [x0], #16",
            "ldr x10, [x0]",
            "mov sp, x10",
            "ret",
            in("x0") next,
            options(noreturn),
        )
    }
}