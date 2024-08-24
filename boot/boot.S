.global _start

_start:
	// ==============================
	// EL3 阶段：切到 EL2
	// ==============================
	mrs  x0, CurrentEL // 读当前异常级别
	lsr  x0, x0, #2
	cmp  x0, #3        // 确认当前在 EL3
	b.ne .             // 不在 EL3 就卡死（方便调试）

	// 配置 SCR_EL3：NS=1（非安全模式），允许 EL2 为 AArch64
	mov x0, #(1 << 10) // SCR_EL3.NS = 1
	msr scr_el3, x0
	isb

	// 设置 EL2 栈（4KB，足够启动用）
	adrp x1, el2_stack_top
	add  x1, x1, :lo12:el2_stack_top
	msr  sp_el2, x1

	// 配置 HCR_EL2：RW=1（EL1 运行在 AArch64）
	mov x0, #(1 << 10) // HCR_EL2.RW = 1
	msr hcr_el2, x0
	isb

	// ERET 跳转到 EL2 入口
	//   示例：配置 SPSR_EL3，让 eret 后进入 EL2
	mov x1, #0x3C0    // EL=2 (0b10), 同时屏蔽 IRQ/FIQ/SError, 选择 SP_EL2
	msr spsr_el3, x1  // 关键：必须在 msr elr_el3 之前/之后设置，只要在 eret 前就行
	adr x0, el2_entry
	msr elr_el3, x0
	eret              // EL3 → EL2

	// -----------------------------------------------------------------------------
	// EL2 入口：配置 VHE + 跳回 EL1
	// -----------------------------------------------------------------------------
el2_entry:
	// 确认现在在 EL2
	mrs  x0, CurrentEL
	lsr  x0, x0, #2
	cmp  x0, #2
	b.ne .             // 不在 EL2 就卡死

	// 关闭 EL2 的 MMU/缓存（避免干扰）
	mov x0, #0
	msr sctlr_el2, x0
	isb

	// 打开 VHE（Cortex-A53 支持，让 EL1 无感运行）
	// HCR_EL2.E2H = 1（启用虚拟主机扩展）
	// HCR_EL2.RW  = 1（保持 EL1 为 AArch64）
	ldr x0, =((1 << 34) | (1 << 10))
	msr hcr_el2, x0
	isb
	msr hcr_el2, x0                  // 重复写入确保配置生效
	isb

	// 设置 EL1 栈（和你原来的 stack_top 完全一致）
	adrp x1, stack_top
	add  x1, x1, :lo12:stack_top
	msr  sp_el1, x1

	// ERET 跳转到你原来的 EL1 启动代码
	adr x0, el1_entry
	msr elr_el2, x0
	eret              // EL2 → EL1

	// -----------------------------------------------------------------------------
	// EL1 入口：你原来的代码，**一行没改**
	// -----------------------------------------------------------------------------
el1_entry:
	// 加载异常向量表基地址到 VBAR_EL1
	ldr x0, =vector_table
	msr VBAR_EL1, x0
	isb

	// 设置 EL1 栈（你原来的逻辑）
	adrp x0, stack_top
	add  x0, x0, :lo12:stack_top
	mov  sp, x0

	// 跳转到你的 C 入口 main
	bl main
	b  .

	// -----------------------------------------------------------------------------
	// 栈定义：新增 EL2 栈，保留你原来的 EL1 栈
	// -----------------------------------------------------------------------------
	// .bss
	// .align 16
	//
	// el2_stack:
	// .space 4096 // EL2 专用栈（4KB）
	//
	// el2_stack_top:
	//
	// stack:
	// .space 4096 // 你原来的 EL1 栈（保持不变）
	//
	// stack_top:
	//
