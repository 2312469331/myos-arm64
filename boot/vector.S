.cpu cortex-a53
//.arm64
.align 11          // 强制2048字节对齐（AArch64硬件要求）
.global vector_table

vector_table:
    // ============= EL0 异常入口 =============
    b el0_sync     // EL0同步异常（未定义指令/对齐）
    .space 12      // 填充至16字节
    b el0_irq      // EL0外部中断
    .space 12
    b el0_fiq      // EL0快速中断
    .space 12
    b el0_serror   // EL0系统错误
    .space 12

    // ============= EL1 异常入口（核心） =============
    b el1_sync     // EL1同步异常（你的对齐异常/系统调用）
    .space 12
    b el1_irq      // EL1外部中断（UART/定时器）
    .space 12
    b el1_fiq      // EL1快速中断
    .space 12
    b el1_serror   // EL1系统错误
    .space 12

// 异常现场保存（通用寄存器入栈）
.macro save_el1_ctx
    sub sp, sp, #34*8
    stp x0, x1, [sp, #0*16]
    stp x2, x3, [sp, #1*16]
    stp x4, x5, [sp, #2*16]
    stp x6, x7, [sp, #3*16]
    stp x8, x9, [sp, #4*16]
    stp x10, x11, [sp, #5*16]
    stp x12, x13, [sp, #6*16]
    stp x14, x15, [sp, #7*16]
    stp x16, x17, [sp, #8*16]
    stp x18, x19, [sp, #9*16]
    stp x20, x21, [sp, #10*16]
    stp x22, x23, [sp, #11*16]
    stp x24, x25, [sp, #12*16]
    stp x26, x27, [sp, #13*16]
    stp x28, x29, [sp, #14*16]
    mov x0, sp
    add x0, x0, #34*8
    str x0, [sp, #30*8]  // 保存SP
    mrs x0, elr_el1
    str x0, [sp, #31*8]  // 保存返回地址
    mrs x0, spsr_el1
    str x0, [sp, #32*8]  // 保存状态寄存器
.endm

// 异常现场恢复
.macro restore_el1_ctx
    ldp x0, x1, [sp, #0*16]
    ldp x2, x3, [sp, #1*16]
    ldp x4, x5, [sp, #2*16]
    ldp x6, x7, [sp, #3*16]
    ldp x8, x9, [sp, #4*16]
    ldp x10, x11, [sp, #5*16]
    ldp x12, x13, [sp, #6*16]
    ldp x14, x15, [sp, #7*16]
    ldp x16, x17, [sp, #8*16]
    ldp x18, x19, [sp, #9*16]
    ldp x20, x21, [sp, #10*16]
    ldp x22, x23, [sp, #11*16]
    ldp x24, x25, [sp, #12*16]
    ldp x26, x27, [sp, #13*16]
    ldp x28, x29, [sp, #14*16]
    ldr x0, [sp, #31*8]
    msr elr_el1, x0
    ldr x0, [sp, #32*8]
    msr spsr_el1, x0
    add sp, sp, #34*8
    eret
.endm

// EL1同步异常处理（跳转C函数）
el1_sync:
    save_el1_ctx
    mov x0, sp
    bl el1_sync_handler  // 调用C语言处理函数
    restore_el1_ctx

// EL1 IRQ中断处理（跳转C函数）
el1_irq:
    save_el1_ctx
    mov x0, sp
    bl el1_irq_handler   // 调用C语言中断函数
    restore_el1_ctx

// 占位函数（未实现异常）
el0_sync: el0_irq: el0_fiq: el0_serror: el1_fiq: el1_serror:
    b .

