// AArch64 三层架构异常向量表
// 段规则：.vectors.el3 / .vectors.el2 / .vectors.el1
// 对齐要求：2048字节（.align 11）— ARMV8-A 硬件强制要求

// ============== 1. EL3 向量表（安全固件层，极简实现） ==============
.align  11               // 保持2048字节对齐（AArch64硬件要求）
.global vector_table_el3

vector_table_el3:
	b      el3_sync   // EL3 同步异常
	.space 12         // 硬件要求的保留空间
	b      el3_irq    // EL3 外部中断
	.space 12         // 硬件要求的保留空间
	b      el3_fiq    // EL3 快速中断
	.space 12         // 硬件要求的保留空间
	b      el3_serror // EL3 系统错误
	.space 12         // 硬件要求的保留空间

	// EL3 异常处理（先写死循环，后续实现安全初始化）
el3_sync: el3_irq: el3_fiq: el3_serror:
	b . // 死循环占位

	// ============== 2. EL2 向量表（虚拟化层，极简实现） ==============
	.align  11               // 保持2048字节对齐
	.global vector_table_el2

vector_table_el2:
	b      el2_sync   // EL2 同步异常
	.space 12         // 硬件要求的保留空间
	b      el2_irq    // EL2 外部中断
	.space 12         // 硬件要求的保留空间
	b      el2_fiq    // EL2 快速中断
	.space 12         // 硬件要求的保留空间
	b      el2_serror // EL2 系统错误
	.space 12         // 硬件要求的保留空间

	// EL2 异常处理（先写死循环，后续实现虚拟化切换）
el2_sync: el2_irq: el2_fiq: el2_serror:
	b . // 死循环占位

	// ============== 3. EL1 向量表（内核/OS 层） ==============
	.align  11               // 保持2048字节对齐（AArch64硬件要求）
	.global vector_table_el1 // 改名为el1，和层级对应

vector_table_el1:
	// ============== EL0 异常入口 ==============
	b      el0_sync   // EL0 同步异常（未定义指令/对齐）
	.space 12         // 硬件要求的保留空间
	b      el0_irq    // EL0 外部中断
	.space 12         // 硬件要求的保留空间
	b      el0_fiq    // EL0 快速中断
	.space 12         // 硬件要求的保留空间
	b      el0_serror // EL0 系统错误
	.space 12         // 硬件要求的保留空间

	// ============== EL1 异常入口（你的核心逻辑） ==============
	b      el1_sync   // EL1 同步异常（对齐异常/系统调用）
	.space 12         // 硬件要求的保留空间
	b      el1_irq    // EL1 外部中断（UART/定时器）
	.space 12         // 硬件要求的保留空间
	b      el1_fiq    // EL1 快速中断
	.space 12         // 硬件要求的保留空间
	b      el1_serror // EL1 系统错误
	.space 12         // 硬件要求的保留空间

	// ============== 你的宏/异常处理函数（完全复用） ==============
	// 异常现场保存（通用寄存器入栈）
	.macro save_el1_ctx
	sub    sp, sp, #34*8
	stp    x0, x1, [sp, #0*16]    // 保存x0-x1
	stp    x2, x3, [sp, #1*16]    // 保存x2-x3
	stp    x4, x5, [sp, #2*16]    // 保存x4-x5
	stp    x6, x7, [sp, #3*16]    // 保存x6-x7
	stp    x8, x9, [sp, #4*16]    // 保存x8-x9
	stp    x10, x11, [sp, #5*16]  // 保存x10-x11
	stp    x12, x13, [sp, #6*16]  // 保存x12-x13
	stp    x14, x15, [sp, #7*16]  // 保存x14-x15
	stp    x16, x17, [sp, #8*16]  // 保存x16-x17
	stp    x18, x19, [sp, #9*16]  // 保存x18-x19
	stp    x20, x21, [sp, #10*16] // 保存x20-x21
	stp    x22, x23, [sp, #11*16] // 保存x22-x23
	stp    x24, x25, [sp, #12*16] // 保存x24-x25
	stp    x26, x27, [sp, #13*16] // 保存x26-x27
	stp    x28, x29, [sp, #14*16] // 保存x28-x29
	mov    x0, sp
	add    x0, x0, #34*8
	str    x0, [sp, #30*8]        // 保存异常发生前的SP指针
	mrs    x0, elr_el1            // ✅ 指令与寄存器之间加空格
	str    x0, [sp, #31*8]        // 保存返回地址（ELR_EL1）
	mrs    x0, spsr_el1           // ✅ 核心修复：mrs 与 spsr_el1 之间必须有空格
	str    x0, [sp, #32*8]        // 保存状态寄存器（SPSR_EL1）
	.endm

	// 异常现场恢复
	.macro restore_el1_ctx
	ldp    x0, x1, [sp, #0*16]
	ldp    x2, x3, [sp, #1*16]
	ldp    x4, x5, [sp, #2*16]
	ldp    x6, x7, [sp, #3*16]
	ldp    x8, x9, [sp, #4*16]
	ldp    x10, x11, [sp, #5*16]
	ldp    x12, x13, [sp, #6*16]
	ldp    x14, x15, [sp, #7*16]
	ldp    x16, x17, [sp, #8*16]
	ldp    x18, x19, [sp, #9*16]
	ldp    x20, x21, [sp, #10*16]
	ldp    x22, x23, [sp, #11*16]
	ldp    x24, x25, [sp, #12*16]
	ldp    x26, x27, [sp, #13*16]
	ldp    x28, x29, [sp, #14*16]
	ldr    x0, [sp, #31*8]
	msr    elr_el1, x0            // 恢复返回地址
	ldr    x0, [sp, #32*8]
	msr    spsr_el1, x0           // 恢复状态寄存器
	add    sp, sp, #34*8          // 释放栈空间
	eret                          // 异常返回
	.endm

	// EL1 同步异常处理（跳转C函数）
el1_sync:
	save_el1_ctx         // 保存CPU现场
	mov x0, sp           // 把栈指针作为参数传给C函数
	bl  el1_sync_handler // 调用C语言异常处理函数
	restore_el1_ctx      // 恢复现场并返回

	// EL1 IRQ 中断处理（跳转C函数）
el1_irq:
	save_el1_ctx        // 保存CPU现场
	mov x0, sp          // 把栈指针作为参数传给C函数
	bl  el1_irq_handler // 调用C语言中断处理函数
	restore_el1_ctx     // 恢复现场并返回

	// 占位函数（未实现的异常统一死循环）
el0_sync: el0_irq: el0_fiq: el0_serror: el1_fiq: el1_serror:
	b . // 死循环，便于调试定位

