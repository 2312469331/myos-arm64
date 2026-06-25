.text

/* 
 * ============================================================================== 
 * ARM64 异常向量表 (EL3, 安全世界) 
 * 严格遵循 ARMv8-A 架构规范 
 * 基地址必须 16KB (0x4000) 对齐 
 * 每个异常入口固定占用 128 字节 (0x80 对齐) 
 * ============================================================================== 
 */ 
.align 11
.global vector_table_el3 
vector_table_el3: 

/* 
 * ============================================================================== 
 * 第 1 组：当前异常等级 (EL3)，使用 SP_EL0 堆栈指针 
 * 偏移地址：0x000 ~ 0x180 
 * ============================================================================== 
 */

/* 
 * 偏移地址：0x000 
 * 功能说明：Current EL (EL3) with SP_EL0 
 *           同步异常 (非外部 abort / 外部 abort，EASE==0) 
 */ 
.align 7 
el3_sp0_sync_e0: 
    b el3_exception_entry

/* 
 * 偏移地址：0x080 
 * 功能说明：Current EL (EL3) with SP_EL0 
 *           普通中断 / 虚拟中断 (IRQ/vIRQ) 
 */ 
.align 7 
el3_sp0_irq: 
    b el3_exception_entry

/* 
 * 偏移地址：0x100 
 * 功能说明：Current EL (EL3) with SP_EL0 
 *           快速中断 / 虚拟快速中断 (FIQ/vFIQ) 
 */ 
.align 7 
el3_sp0_fiq: 
    b el3_exception_entry

/* 
 * 偏移地址：0x180 
 * 功能说明：Current EL (EL3) with SP_EL0 
 *           同步外部异常 (EASE==1) / 系统错误 / 虚拟系统错误 / 委托系统错误 (SError) 
 */ 
.align 7 
el3_sp0_serror: 
    b el3_exception_entry

/* 
 * ============================================================================== 
 * 第 2 组：当前异常等级 (EL3)，使用 SP_EL3 堆栈指针 
 * 偏移地址：0x200 ~ 0x380 
 * ============================================================================== 
 */

/* 
 * 偏移地址：0x200 
 * 功能说明：Current EL (EL3) with SP_EL3 
 *           同步异常 (非外部 abort / 外部 abort，EASE==0) 
 */ 
.align 7 
el3_spx_sync_e0: 
    b el3_exception_entry

/* 
 * 偏移地址：0x280 
 * 功能说明：Current EL (EL3) with SP_EL3 
 *           普通中断 / 虚拟中断 (IRQ/vIRQ) 
 */ 
.align 7 
el3_spx_irq: 
    b el3_exception_entry

/* 
 * 偏移地址：0x300 
 * 功能说明：Current EL (EL3) with SP_EL3 
 *           快速中断 / 虚拟快速中断 (FIQ/vFIQ) 
 */ 
.align 7 
el3_spx_fiq: 
    b el3_exception_entry

/* 
 * 偏移地址：0x380 
 * 功能说明：Current EL (EL3) with SP_EL3 
 *           同步外部异常 (EASE==1) / 系统错误 / 虚拟系统错误 / 委托系统错误 (SError) 
 */ 
.align 7 
el3_spx_serror: 
    b el3_exception_entry

/* 
 * ============================================================================== 
 * 第 3 组：来自下层 64 位异常等级 (Lower EL)，AArch64 状态 
 * 偏移地址：0x400 ~ 0x580 
 * ============================================================================== 
 */

/* 
 * 偏移地址：0x400 
 * 功能说明：Lower EL (AArch64 状态) -> EL3 
 *           同步异常 (非外部 abort / 外部 abort，EASE==0) 
 */ 
.align 7 
el3_lower_a64_sync_e0: 
    b el3_exception_entry

/* 
 * 偏移地址：0x480 
 * 功能说明：Lower EL (AArch64 状态) -> EL3 
 *           普通中断 / 虚拟中断 (IRQ/vIRQ) 
 */ 
.align 7 
el3_lower_a64_irq: 
    b el3_exception_entry

/* 
 * 偏移地址：0x500 
 * 功能说明：Lower EL (AArch64 状态) -> EL3 
 *           快速中断 / 虚拟快速中断 (FIQ/vFIQ) 
 */ 
.align 7 
el3_lower_a64_fiq: 
    b el3_exception_entry

/* 
 * 偏移地址：0x580 
 * 功能说明：Lower EL (AArch64 状态) -> EL3 
 *           同步外部异常 (EASE==1) / 系统错误 / 虚拟系统错误 / 委托系统错误 (SError) 
 */ 
.align 7 
el3_lower_a64_serror: 
    b el3_exception_entry

/* 
 * ============================================================================== 
 * 第 4 组：来自下层 32 位异常等级 (Lower EL)，AArch32 状态 
 * 偏移地址：0x600 ~ 0x780 
 * ============================================================================== 
 */

/* 
 * 偏移地址：0x600 
 * 功能说明：Lower EL (AArch32 状态) -> EL3 
 *           同步异常 (非外部 abort / 外部 abort，EASE==0) 
 */ 
.align 7 
el3_lower_a32_sync_e0: 
    b el3_exception_entry

/* 
 * 偏移地址：0x680 
 * 功能说明：Lower EL (AArch32 状态) -> EL3 
 *           普通中断 / 虚拟中断 (IRQ/vIRQ) 
 */ 
.align 7 
el3_lower_a32_irq: 
    b el3_exception_entry

/* 
 * 偏移地址：0x700 
 * 功能说明：Lower EL (AArch32 状态) -> EL3 
 *           快速中断 / 虚拟快速中断 (FIQ/vFIQ) 
 */ 
.align 7 
el3_lower_a32_fiq: 
    b el3_exception_entry

/* 
 * 偏移地址：0x780 
 * 功能说明：Lower EL (AArch32 状态) -> EL3 
 *           同步外部异常 (EASE==1) / 系统错误 / 虚拟系统错误 / 委托系统错误 (SError) 
 */ 
.align 7 
el3_lower_a32_serror: 
    b el3_exception_entry

/* 
 * ============================================================================== 
 * EL3 异常统一处理入口 
 * ============================================================================== 
 */
el3_exception_entry:
    /* 保存所有通用寄存器 */
    stp x1, x0, [sp, #-16]!
    stp x2, x3, [sp, #-16]!
    stp x4, x5, [sp, #-16]!
    stp x6, x7, [sp, #-16]!
    stp x8, x9, [sp, #-16]!
    stp x10, x11, [sp, #-16]!
    stp x12, x13, [sp, #-16]!
    stp x14, x15, [sp, #-16]!
    stp x16, x17, [sp, #-16]!
    stp x18, x19, [sp, #-16]!
    stp x20, x21, [sp, #-16]!
    stp x22, x23, [sp, #-16]!
    stp x24, x25, [sp, #-16]!
    stp x26, x27, [sp, #-16]!
    stp x28, x29, [sp, #-16]!
    stp x30, xzr, [sp, #-16]!

    /* 保存 EL3 专用返回地址和状态寄存器 */
    mrs x0, elr_el3
    mrs x1, spsr_el3
    stp x1, x0, [sp, #-16]!

    /* 调用 C 语言异常处理函数 */
    bl c_exception_handler_el3

    /* 恢复 EL3 专用返回地址和状态寄存器 */
    ldp x0, x1, [sp], #16
    msr elr_el3, x0
    msr spsr_el3, x1

    /* 恢复所有通用寄存器 */
    ldp x30, xzr, [sp], #16
    ldp x28, x29, [sp], #16
    ldp x26, x27, [sp], #16
    ldp x24, x25, [sp], #16
    ldp x22, x23, [sp], #16
    ldp x20, x21, [sp], #16
    ldp x18, x19, [sp], #16
    ldp x16, x17, [sp], #16
    ldp x14, x15, [sp], #16
    ldp x12, x13, [sp], #16
    ldp x10, x11, [sp], #16
    ldp x8, x9, [sp], #16
    ldp x6, x7, [sp], #16
    ldp x4, x5, [sp], #16
    ldp x2, x3, [sp], #16
    ldp x0, x1, [sp], #16

    /* 异常返回 (EL3 -> 原异常等级) */
    eret

/* 
 * ============================================================================== 
 * ARM64 异常向量表 (EL2, 虚拟化层) 
 * 严格遵循 ARMv8-A 架构规范 
 * ============================================================================== 
 */ 
.align 11
.global vector_table_el2 
vector_table_el2: 

/* 
 * ============================================================================== 
 * 第 1 组：当前异常等级 (EL2)，使用 SP_EL0 堆栈指针 
 * 偏移地址：0x000 ~ 0x180 
 * ============================================================================== 
 */

/* 
 * 偏移地址：0x000 
 * 功能说明：Current EL (EL2) with SP_EL0 
 *           同步异常 (非外部 abort / 外部 abort，EASE==0) 
 */ 
.align 7 
el2_sp0_sync_e0: 
    b el2_exception_entry

/* 
 * 偏移地址：0x080 
 * 功能说明：Current EL (EL2) with SP_EL0 
 *           普通中断 / 虚拟中断 (IRQ/vIRQ) 
 */ 
.align 7 
el2_sp0_irq: 
    b el2_exception_entry

/* 
 * 偏移地址：0x100 
 * 功能说明：Current EL (EL2) with SP_EL0 
 *           快速中断 / 虚拟快速中断 (FIQ/vFIQ) 
 */ 
.align 7 
el2_sp0_fiq: 
    b el2_exception_entry

/* 
 * 偏移地址：0x180 
 * 功能说明：Current EL (EL2) with SP_EL0 
 *           同步外部异常 (EASE==1) / 系统错误 / 虚拟系统错误 / 委托系统错误 (SError) 
 */ 
.align 7 
el2_sp0_serror: 
    b el2_exception_entry

/* 
 * ============================================================================== 
 * 第 2 组：当前异常等级 (EL2)，使用 SP_EL2 堆栈指针 
 * 偏移地址：0x200 ~ 0x380 
 * ============================================================================== 
 */

/* 
 * 偏移地址：0x200 
 * 功能说明：Current EL (EL2) with SP_EL2 
 *           同步异常 (非外部 abort / 外部 abort，EASE==0) 
 */ 
.align 7 
el2_spx_sync_e0: 
    b el2_exception_entry

/* 
 * 偏移地址：0x280 
 * 功能说明：Current EL (EL2) with SP_EL2 
 *           普通中断 / 虚拟中断 (IRQ/vIRQ) 
 */ 
.align 7 
el2_spx_irq: 
    b el2_exception_entry

/* 
 * 偏移地址：0x300 
 * 功能说明：Current EL (EL2) with SP_EL2 
 *           快速中断 / 虚拟快速中断 (FIQ/vFIQ) 
 */ 
.align 7 
el2_spx_fiq: 
    b el2_exception_entry

/* 
 * 偏移地址：0x380 
 * 功能说明：Current EL (EL2) with SP_EL2 
 *           同步外部异常 (EASE==1) / 系统错误 / 虚拟系统错误 / 委托系统错误 (SError) 
 */ 
.align 7 
el2_spx_serror: 
    b el2_exception_entry

/* 
 * ============================================================================== 
 * 第 3 组：来自下层 64 位异常等级 (Lower EL)，AArch64 状态 
 * 偏移地址：0x400 ~ 0x580 
 * ============================================================================== 
 */

/* 
 * 偏移地址：0x400 
 * 功能说明：Lower EL (AArch64 状态) -> EL2 
 *           同步异常 (非外部 abort / 外部 abort，EASE==0) 
 */ 
.align 7 
el2_lower_a64_sync_e0: 
    b el2_exception_entry

/* 
 * 偏移地址：0x480 
 * 功能说明：Lower EL (AArch64 状态) -> EL2 
 *           普通中断 / 虚拟中断 (IRQ/vIRQ) 
 */ 
.align 7 
el2_lower_a64_irq: 
    b el2_exception_entry

/* 
 * 偏移地址：0x500 
 * 功能说明：Lower EL (AArch64 状态) -> EL2 
 *           快速中断 / 虚拟快速中断 (FIQ/vFIQ) 
 */ 
.align 7 
el2_lower_a64_fiq: 
    b el2_exception_entry

/* 
 * 偏移地址：0x580 
 * 功能说明：Lower EL (AArch64 状态) -> EL2 
 *           同步外部异常 (EASE==1) / 系统错误 / 虚拟系统错误 / 委托系统错误 (SError) 
 */ 
.align 7 
el2_lower_a64_serror: 
    b el2_exception_entry

/* 
 * ============================================================================== 
 * 第 4 组：来自下层 32 位异常等级 (Lower EL)，AArch32 状态 
 * 偏移地址：0x600 ~ 0x780 
 * ============================================================================== 
 */

/* 
 * 偏移地址：0x600 
 * 功能说明：Lower EL (AArch32 状态) -> EL2 
 *           同步异常 (非外部 abort / 外部 abort，EASE==0) 
 */ 
.align 7 
el2_lower_a32_sync_e0: 
    b el2_exception_entry

/* 
 * 偏移地址：0x680 
 * 功能说明：Lower EL (AArch32 状态) -> EL2 
 *           普通中断 / 虚拟中断 (IRQ/vIRQ) 
 */ 
.align 7 
el2_lower_a32_irq: 
    b el2_exception_entry

/* 
 * 偏移地址：0x700 
 * 功能说明：Lower EL (AArch32 状态) -> EL2 
 *           快速中断 / 虚拟快速中断 (FIQ/vFIQ) 
 */ 
.align 7 
el2_lower_a32_fiq: 
    b el2_exception_entry

/* 
 * 偏移地址：0x780 
 * 功能说明：Lower EL (AArch32 状态) -> EL2 
 *           同步外部异常 (EASE==1) / 系统错误 / 虚拟系统错误 / 委托系统错误 (SError) 
 */ 
.align 7 
el2_lower_a32_serror: 
    b el2_exception_entry

/* 
 * ============================================================================== 
 * EL2 异常统一处理入口 
 * ============================================================================== 
 */
el2_exception_entry:
    /* 保存所有通用寄存器 */
    stp x1, x0, [sp, #-16]!
    stp x2, x3, [sp, #-16]!
    stp x4, x5, [sp, #-16]!
    stp x6, x7, [sp, #-16]!
    stp x8, x9, [sp, #-16]!
    stp x10, x11, [sp, #-16]!
    stp x12, x13, [sp, #-16]!
    stp x14, x15, [sp, #-16]!
    stp x16, x17, [sp, #-16]!
    stp x18, x19, [sp, #-16]!
    stp x20, x21, [sp, #-16]!
    stp x22, x23, [sp, #-16]!
    stp x24, x25, [sp, #-16]!
    stp x26, x27, [sp, #-16]!
    stp x28, x29, [sp, #-16]!
    stp x30, xzr, [sp, #-16]!

    /* 保存 EL2 专用返回地址和状态寄存器 */
    mrs x0, elr_el2
    mrs x1, spsr_el2
    stp x1, x0, [sp, #-16]!

    /* 调用 C 语言异常处理函数 */
    bl c_exception_handler_el2

    /* 恢复过程中禁用中断，防止嵌套破坏栈 */
    msr daifset, #0xf

    /* 恢复 EL2 专用返回地址和状态寄存器 */
    ldp x0, x1, [sp], #16
    msr elr_el2, x0
    msr spsr_el2, x1

    /* 恢复所有通用寄存器 */
    ldp x30, xzr, [sp], #16
    ldp x28, x29, [sp], #16
    ldp x26, x27, [sp], #16
    ldp x24, x25, [sp], #16
    ldp x22, x23, [sp], #16
    ldp x20, x21, [sp], #16
    ldp x18, x19, [sp], #16
    ldp x16, x17, [sp], #16
    ldp x14, x15, [sp], #16
    ldp x12, x13, [sp], #16
    ldp x10, x11, [sp], #16
    ldp x8, x9, [sp], #16
    ldp x6, x7, [sp], #16
    ldp x4, x5, [sp], #16
    ldp x2, x3, [sp], #16
    ldp x0, x1, [sp], #16

    /* 异常返回 (EL2 -> 原异常等级) */
    eret

/* 
 * ============================================================================== 
 * ARM64 异常向量表 (EL1, 内核层) 
 * 严格遵循 ARMv8-A 架构规范 
 * ============================================================================== 
 */ 
.align 11
.global vector_table_el1 
vector_table_el1: 

/* 
 * ============================================================================== 
 * 第 1 组：当前异常等级 (EL1)，使用 SP_EL0 堆栈指针 
 * 偏移地址：0x000 ~ 0x180 
 * ============================================================================== 
 */

/* 
 * 偏移地址：0x000 
 * 功能说明：Current EL (EL1) with SP_EL0 
 *           同步异常 (非外部 abort / 外部 abort，EASE==0) 
 */ 
.align 7 
el1_sp0_sync_e0: 
    msr daifset, #0xf
    // 保存 x0
    stp x1, x0, [sp, #-16]!

    // 把 0 放入 x0
    mov x0, #0  // 异常类型 (0 = 同步异常)
    b el1_exception_entry

/* 
 * 偏移地址：0x080 
 * 功能说明：Current EL (EL1) with SP_EL0 
 *           普通中断 / 虚拟中断 (IRQ/vIRQ) 
 */ 
.align 7 
el1_sp0_irq: 
    msr daifset, #0xf
    // 保存 x0
    stp x1, x0, [sp, #-16]!

    // 把 1 放入 x0
    mov x0, #1  // 异常类型 (1 = 中断)
    b el1_exception_entry

/* 
 * 偏移地址：0x100 
 * 功能说明：Current EL (EL1) with SP_EL0 
 *           快速中断 / 虚拟快速中断 (FIQ/vFIQ) 
 */ 
.align 7 
el1_sp0_fiq: 
    msr daifset, #0xf
    // 保存 x0
    stp x1, x0, [sp, #-16]!

    // 把 2 放入 x0
    mov x0, #2  // 异常类型 (2 = 中断)
    b el1_exception_entry

/* 
 * 偏移地址：0x180 
 * 功能说明：Current EL (EL1) with SP_EL0 
 *           同步外部异常 (EASE==1) / 系统错误 / 虚拟系统错误 / 委托系统错误 (SError) 
 */ 
.align 7 
el1_sp0_serror: 
    msr daifset, #0xf
    // 保存 x0
    stp x1, x0, [sp, #-16]!

    // 把 3 放入 x0
    mov x0, #3  // 异常类型 (3 = 系统错误)
    b el1_exception_entry

/* 
 * ============================================================================== 
 * 第 2 组：当前异常等级 (EL1)，使用 SP_EL1 堆栈指针 
 * 偏移地址：0x200 ~ 0x380 
 * ============================================================================== 
 */

/* 
 * 偏移地址：0x200 
 * 功能说明：Current EL (EL1) with SP_EL1 
 *           同步异常 (非外部 abort / 外部 abort，EASE==0) 
 */ 
.align 7 
el1_spx_sync_e0: 
    msr daifset, #0xf
    // 保存 x0
    stp x1, x0, [sp, #-16]!

    // 把 0 放入 x0
    mov x0, #0  // 异常类型 (0 = 同步异常)
    b el1_exception_entry


/* 
 * 偏移地址：0x280 
 * 功能说明：Current EL (EL1) with SP_EL1 
 *           普通中断 / 虚拟中断 (IRQ/vIRQ) 
 */ 
.align 7 
el1_spx_irq: 
    msr daifset, #0xf
    // 保存 x0
    stp x1, x0, [sp, #-16]!

    // 把 0 放入 x0
    mov x0, #1  // 异常类型 (1 = 中断)
    b el1_exception_entry

/* 
 * 偏移地址：0x300 
 * 功能说明：Current EL (EL1) with SP_EL1 
 *           快速中断 / 虚拟快速中断 (FIQ/vFIQ) 
 */ 
.align 7 
el1_spx_fiq: 
    msr daifset, #0xf
    // 保存 x0
    stp x1, x0, [sp, #-16]!

    // 把 0 放入 x0
    mov x0, #2  // 异常类型 (2 = 中断)
    b el1_exception_entry

/* 
 * 偏移地址：0x380 
 * 功能说明：Current EL (EL1) with SP_EL1 
 *           同步外部异常 (EASE==1) / 系统错误 / 虚拟系统错误 / 委托系统错误 (SError) 
 */ 
.align 7 
el1_spx_serror: 
    msr daifset, #0xf
    // 保存 x0
    stp x1, x0, [sp, #-16]!

    // 把 0 放入 x0
    mov x0, #3  // 异常类型 (3 = 系统错误)
    b el1_exception_entry

/* 
 * ============================================================================== 
 * 第 3 组：来自下层 64 位异常等级 (Lower EL)，AArch64 状态 
 * 偏移地址：0x400 ~ 0x580 
 * ============================================================================== 
 */

/* 
 * 偏移地址：0x400 
 * 功能说明：Lower EL (AArch64 状态) -> EL1 
 *           同步异常 (非外部 abort / 外部 abort，EASE==0) 
 */ 
.align 7 
el1_lower_a64_sync_e0: 
    msr daifset, #0xf
    stp x1, x0, [sp, #-16]!
    mov x0, #0  // 异常类型 (0 = 同步异常)
    b el1_exception_entry

/* 
 * 偏移地址：0x480 
 * 功能说明：Lower EL (AArch64 状态) -> EL1 
 *           普通中断 / 虚拟中断 (IRQ/vIRQ) 
 */ 
.align 7 
el1_lower_a64_irq: 
    msr daifset, #0xf
    stp x1, x0, [sp, #-16]!
    mov x0, #1  // 异常类型 (1 = 中断)
    b el1_exception_entry

/* 
 * 偏移地址：0x500 
 * 功能说明：Lower EL (AArch64 状态) -> EL1 
 *           快速中断 / 虚拟快速中断 (FIQ/vFIQ) 
 */ 
.align 7 
el1_lower_a64_fiq: 
    msr daifset, #0xf
    stp x1, x0, [sp, #-16]!
    mov x0, #2  // 异常类型 (2 = 中断)
    b el1_exception_entry

/* 
 * 偏移地址：0x580 
 * 功能说明：Lower EL (AArch64 状态) -> EL1 
 *           同步外部异常 (EASE==1) / 系统错误 / 虚拟系统错误 / 委托系统错误 (SError) 
 */ 
.align 7 
el1_lower_a64_serror: 
    msr daifset, #0xf
    stp x1, x0, [sp, #-16]!
    mov x0, #3  // 异常类型 (3 = 系统错误)
    b el1_exception_entry

/* 
 * ============================================================================== 
 * 第 4 组：来自下层 32 位异常等级 (Lower EL)，AArch32 状态 
 * 偏移地址：0x600 ~ 0x780 
 * ============================================================================== 
 */

/* 
 * 偏移地址：0x600 
 * 功能说明：Lower EL (AArch32 状态) -> EL1 
 *           同步异常 (非外部 abort / 外部 abort，EASE==0) 
 */ 
.align 7 
el1_lower_a32_sync_e0: 
    msr daifset, #0xf
    stp x1, x0, [sp, #-16]!
    mov x0, #0  // 异常类型 (0 = 同步异常)
    b el1_exception_entry

/* 
 * 偏移地址：0x680 
 * 功能说明：Lower EL (AArch32 状态) -> EL1 
 *           普通中断 / 虚拟中断 (IRQ/vIRQ) 
 */ 
.align 7 
el1_lower_a32_irq: 
    msr daifset, #0xf
    stp x1, x0, [sp, #-16]!
    mov x0, #1  // 异常类型 (1 = 中断)
    b el1_exception_entry

/* 
 * 偏移地址：0x700 
 * 功能说明：Lower EL (AArch32 状态) -> EL1 
 *           快速中断 / 虚拟快速中断 (FIQ/vFIQ) 
 */ 
.align 7 
el1_lower_a32_fiq: 
    msr daifset, #0xf
    stp x1, x0, [sp, #-16]!
    mov x0, #2  // 异常类型 (2 = 中断)
    b el1_exception_entry

/* 
 * 偏移地址：0x780 
 * 功能说明：Lower EL (AArch32 状态) -> EL1 
 *           同步外部异常 (EASE==1) / 系统错误 / 虚拟系统错误 / 委托系统错误 (SError) 
 */ 
.align 7 
el1_lower_a32_serror: 
    msr daifset, #0xf
    stp x1, x0, [sp, #-16]!
    mov x0, #3  // 异常类型 (3 = 系统错误)
    b el1_exception_entry

/* 
 * ============================================================================== 
 * EL1 异常统一处理入口 
 * ============================================================================== 
 */
el1_exception_entry:
    /* 保存所有通用寄存器（pair 反转，让结构体字段递减） */
    stp x3, x2, [sp, #-16]!
    stp x5, x4, [sp, #-16]!
    stp x7, x6, [sp, #-16]!
    stp x9, x8, [sp, #-16]!
    stp x11, x10, [sp, #-16]!
    stp x13, x12, [sp, #-16]!
    stp x15, x14, [sp, #-16]!
    stp x17, x16, [sp, #-16]!
    stp x19, x18, [sp, #-16]!
    stp x21, x20, [sp, #-16]!
    stp x23, x22, [sp, #-16]!
    stp x25, x24, [sp, #-16]!
    stp x27, x26, [sp, #-16]!
    stp x29, x28, [sp, #-16]!
    stp xzr, x30, [sp, #-16]!

    /* 保存 EL1 专用返回地址和状态寄存器 */
    mrs x2, elr_el1
    mrs x3, spsr_el1
    stp x2, x3, [sp, #-16]!
    /* 把 ex_type 保存到调用方保存寄存器 x19，防止嵌套 IRQ+调度 破坏 x0 */
    mov x19, x0
    /* 新增：把快照栈地址存入x1，作为第二个参数 */
    mov x1, sp              
              
    /* 检查嵌套深度，限制最大4层 */
    adrp x2, irq_nest_level
    add x2, x2, :lo12:irq_nest_level
    ldr x3, [x2]
    add x3, x3, #1
    str x3, [x2]
    cmp x3, #4
    b.gt 1f
    /* 嵌套深度未超限，启用中断支持嵌套 */
    msr daifclr, #0xf
1:
    /* 从 x19 恢复 ex_type（防止嵌套 IRQ+调度破坏 x0） */
    mov x0, x19
              
    /* 调用 C 语言异常处理函数 */
    bl c_exception_handler_el1

    /* 恢复过程中禁用中断，防止嵌套破坏栈 */
    msr daifset, #0xf

    /* 递减嵌套深度 */
    adrp x2, irq_nest_level
    add x2, x2, :lo12:irq_nest_level
    ldr x3, [x2]
    sub x3, x3, #1
    str x3, [x2]

    .global el1_exception_exit
el1_exception_exit:
    ldp x2, x3, [sp], #16
    msr elr_el1, x2
    msr spsr_el1, x3
    
    /* 恢复所有通用寄存器（pair 反转，对应保存顺序） */
    ldp x2, x3, [sp], #16    // x2=xzr(丢弃), x3=x30
    mov x30, x3
    ldp x29, x28, [sp], #16
    ldp x27, x26, [sp], #16
    ldp x25, x24, [sp], #16
    ldp x23, x22, [sp], #16
    ldp x21, x20, [sp], #16
    ldp x19, x18, [sp], #16
    ldp x17, x16, [sp], #16
    ldp x15, x14, [sp], #16
    ldp x13, x12, [sp], #16
    ldp x11, x10, [sp], #16
    ldp x9, x8, [sp], #16
    ldp x7, x6, [sp], #16
    ldp x5, x4, [sp], #16
    ldp x3, x2, [sp], #16
    ldp x1, x0, [sp], #16

    /* 异常返回 (EL1 -> 原异常等级) */
    eret

/* 
 * 嵌套深度限制变量 
 */
.data
.global irq_nest_level
irq_nest_level:
    .dword 0
