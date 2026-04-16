// 必须放在文件最开头，优先级高于编译命令的 -march
.arch    armv8-a
.section .boot.text, "ax" // "ax" 表示可分配(Allocatable) + 可执行(Executable)

/* =========================================================
 * boot.S - ARM64 三层启动代码 (EL3 -> EL2 -> EL1 -> main)
 * 配合 linker.ld 使用
* ========================================================= */
.global _start
/* =========================================================
 * EL3 入口 (Secure Monitor)
* ========================================================= */
_start:
	// 0. 保存 QEMU 传递的 DTB 地址 (x0 寄存器)
	mov x21, x0
	
	// 1. 初始化 EL3 栈指针 (使用链接脚本符号)
	ldr x0, =el3_stack_top_phys
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
	ldr x0, =el2_stack_top_phys
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
	// 1. 加载 EL1 异常向量表（没问题）
	ldr x0, =vector_table_el1
	msr VBAR_EL1, x0
	isb
/* ==========================================
       阶段 3: 初始化页表 (MMU 关闭，使用物理地址)
       ========================================== */
	// 设置栈指针 (物理地址)
	ldr x0, =el1_stack_top_phys
	mov sp, x0

	// 调用 C 函数初始化页表
	bl init_page_tables

/* ==========================================
       阶段 4: 配置系统寄存器
       ========================================== */
	// 设置 MAIR_EL1
	bl  get_mair_el1
	msr mair_el1, x0

	// 设置 TCR_EL1
	bl  get_tcr_el1
	msr tcr_el1, x0

	// 设置 TTBR0_EL1 (1:1 映射)
	bl  get_ttbr0_el1
	msr ttbr0_el1, x0

	// 设置 TTBR1_EL1 (虚拟地址映射)
	bl  get_ttbr1_el1
	msr ttbr1_el1, x0

	isb

/* ==========================================
       阶段 5: 开启 MMU
       ========================================== */
	// 设置 MMU 使能位

	// 写入 SCTLR_EL1
	// msr sctlr_el1, x0
	// bl  get_sctlr_el1

	// 3. 关键：刷新 TLB，清除旧映射缓存
	tlbi vmalle1
	dsb  sy
	isb
	mrs  x0, sctlr_el1
	orr  x0, x0, #(1 << 0)  // 置位 M (MMU Enable)
	orr  x0, x0, #(1 << 2)  // 置位 C (D-Cache Enable)
	orr  x0, x0, #(1 << 12) // 置位 I (I-Cache Enable)
	msr  sctlr_el1, x0

	tlbi vmalle1 // 刷新TLB
	dsb  sy
	isb

/* ==========================================
       阶段 6: 切换栈到虚拟地址
       ========================================== */
	ldr x0, =el1_stack_top
	mov sp, x0

/* ==========================================
       阶段 7: 跳转到 main (虚拟地址)
       ========================================== */
	ldr x1, =main
	mov x0, x21  // 将 DTB 地址传递给 main 函数

	// 开启 MMU 代码...
	tlbi vmalle1
	dsb  sy
	isb
	blr  x1

	// 死循环
hang:
	wfi
	b hang
