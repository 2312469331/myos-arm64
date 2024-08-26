// 必须放在文件最开头，优先级高于编译命令的 -march
.arch armv8-a

/* =========================================================
 * boot.S - ARM64 三层启动代码 (EL3 -> EL2 -> EL1 -> main)
 * 配合 linker.ld 使用
* ========================================================= */
.global _start
/* =========================================================
 * EL3 入口 (Secure Monitor)
* ========================================================= */
_start:
	// 1. 初始化 EL3 栈指针 (使用链接脚本符号)
	ldr x0, =el3_stack_top
	mov sp, x0

	// 2. 配置 SCR_EL3 (设置 EL2 为非安全 AArch64)
	// NS=1(非安全), RW=1(AArch64), HCE=1(允许 HVC), SMD=1(禁止 SMC)
	mov x0, #0x541
	msr scr_el3, x0
	isb

	// 3. 配置 HCR_EL2 (预设 EL1 为 AArch64)
	// RW=1 (EL1/EL0 使用 AArch64)
	mov x0, #(1 << 31)
	msr hcr_el2, x0
	isb

	// 4. 配置 SPSR_EL3 (目标 EL2h, 屏蔽中断)
	// M=01001 (EL2h), D/A/I/F=1 (屏蔽中断)
	mov x0, #0x3C9
	msr spsr_el3, x0

	// 5. 设置 EL2 入口地址
	ldr x0, =el2_entry
	msr elr_el3, x0

	// 6. 跳转到 EL2
	eret

/* =========================================================
 * EL2 入口 (Hypervisor)
* ========================================================= */
el2_entry:
	// 1. 初始化 EL2 栈指针 (供 EL2 阶段使用)
	ldr x0, =el2_stack_top
	mov sp, x0

	// 3. 配置 HCR_EL2 (确保 EL1 使用 AArch64)
	mov x0, #(1 << 31)
	msr hcr_el2, x0
	isb

	// 4. 配置 SPSR_EL2 (目标 EL1h, 屏蔽中断)

	// EL1h 模式（DAIF 屏蔽不变，仅修改 M[3:0]）
	mov x0, #0x3c5   // 0x3c9 - 0x4 = 0x3c5 → M[3:0] = 0b0101（EL1h）
	msr spsr_el2, x0

	// 5. 设置 EL1 入口地址
	ldr x0, =el1_entry
	msr elr_el2, x0

	// 6. 跳转到 EL1
	eret

/* ==================================================
   EL1 入口 (Kernel Mode)
   ================================================== */
el1_entry:
	// 加载 EL1 异常向量表
	ldr x0, =vector_table_el1
	msr VBAR_EL1, x0
	isb

	// 2. 配置 EL1 栈指针 (必须在 EL2 设置 SP_EL1，供下一级使用)
	ldr x0, =el1_stack_top
	mov sp, x0

	// 2. 关闭 MMU/Cache（确保物理地址访问，安全起见）
	mov x0, #0
	msr sctlr_el1, x0
	isb

	// 3. 准备 main 函数参数（可选，这里传 0）
	mov x0, #0
	mov x1, #0

	// 4. 调用 C 语言 main 函数
	bl main

	// 5. main 返回后死循环（防止跑飞）
halt:
	wfe
	b halt

