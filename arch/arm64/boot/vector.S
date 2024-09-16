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

	.global vector_table_el1

vector_table_el1:

	// Current EL1, SP_EL0
	b      el1_sp0_sync
	.space 128-4
	b      el1_sp0_irq
	.space 128-4
	b      el1_sp0_fiq
	.space 128-4
	b      el1_sp0_serror
	.space 128-4

	// Current EL1, SP_EL1
	b      el1_spx_sync
	.space 128-4
	b      el1_spx_irq
	.space 128-4
	b      el1_spx_fiq
	.space 128-4
	b      el1_spx_serror
	.space 128-4

	// Lower EL (EL0 AArch64) → EL1
	b      el1_lower_aarch64_sync
	.space 128-4
	b      el1_lower_aarch64_irq
	.space 128-4
	b      el1_lower_aarch64_fiq
	.space 128-4
	b      el1_lower_aarch64_serror
	.space 128-4

	// Lower EL (EL0 AArch32) → EL1
	b      el1_lower_aarch32_sync
	.space 128-4
	b      el1_lower_aarch32_irq
	.space 128-4
	b      el1_lower_aarch32_fiq
	.space 128-4
	b      el1_lower_aarch32_serror
	.space 128-4

	// 所有异常统一跳这里：保存现场
el1_exception_entry:
	// 保存所有通用寄存器 x0~x30
	stp x0, x1, [sp, #-16]!
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
	stp x30, lr, [sp, #-16]!

	// 把 ELR_EL1、SPSR_EL1 也存到栈上
	mrs x0, elr_el1
	mrs x1, spsr_el1
	stp x0, x1, [sp, #-16]!

	// 跳 C 语言异常处理函数
	bl c_exception_handler

	// 恢复 ELR & SPSR
	ldp x0, x1, [sp], #16
	msr elr_el1, x0
	msr spsr_el1, x1

	// 恢复所有通用寄存器
	ldp x30, lr, [sp], #16
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

	// 异常返回（手册规定唯一合法返回指令）
	eret

	// EL1 全部异常都指向统一入口
el1_sp0_sync:
el1_sp0_irq:
el1_sp0_fiq:
el1_sp0_serror:
el1_spx_sync:
el1_spx_irq:
el1_spx_fiq:
el1_spx_serror:
el1_lower_aarch64_sync:
el1_lower_aarch64_irq:
el1_lower_aarch64_fiq:
el1_lower_aarch64_serror:
el1_lower_aarch32_sync:
el1_lower_aarch32_irq:
el1_lower_aarch32_fiq:
el1_lower_aarch32_serror:
	b el1_exception_entry
