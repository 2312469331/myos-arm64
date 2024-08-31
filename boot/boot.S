// 必须放在文件最开头，优先级高于编译命令的 -march
.arch    armv8-a
.section .text.boot, "ax" // "ax" 表示可分配(Allocatable) + 可执行(Executable)

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
	// 1. 加载 EL1 异常向量表（没问题）
	ldr x0, =vector_table_el1
	msr VBAR_EL1, x0
	isb

	// 2. 配置 EL1 物理栈（没问题，用物理地址）
	ldr x0, =el1_stack_top_phys
	mov sp, x0

	// --------------------------
	// 核心修正1：先填页表！！！
	// --------------------------
	bl init_page_tables // 先把 pgd/init_pgt 页表项填好

	// --------------------------
	// 核心修正2：同时配置 TTBR0 + TTBR1（都指向 pgd）
	// --------------------------
	ldr x0, =init_pgt // pgd = init_pgt（物理地址，和 C 代码一致）
	msr ttbr0_el1, x0 // TTBR0：0-255 项 → 1:1 物理映射
	msr ttbr1_el1, x0 // TTBR1：256-511 项 → 高地址虚拟映射
	isb

	// --------------------------
	// 1. 定义 MAIR_EL1 配置值
	// 格式: Attr7 << 56 | Attr6 << 48 | Attr5 << 40 | Attr4 << 32 | Attr3 << 24 | Attr2 << 16 | Attr1 << 8 | Attr0
	// --------------------------
	// Attr0: 0x00 → Device-nGnRnE (最严格的设备内存, 用于外设寄存器)
	// Attr1: 0x44 → Normal, Inner Non-cacheable + Outer Non-cacheable (无缓存普通内存)
	// Attr2: 0xAA → Normal, Inner Write-Through Non-transient + Outer Write-Through Non-transient (写直达缓存)
	// Attr3: 0xFF → Normal, Inner Write-Back Non-transient + Outer Write-Back Non-transient (高性能写回缓存, 用于RAM)
	// Attr4~Attr7: 复用 0xFF (高性能写回缓存, 方便后续扩展)
	// --------------------------
	ldr x0, =(0xFF << 56) | (0xFF << 48) | (0xFF << 40) | (0xFF << 32) | (0xFF << 24) | (0xAA << 16) | (0x44 << 8) | (0x00 << 0)

	// 2. 写入 MAIR_EL1 寄存器
	msr mair_el1, x0

	// 3. 同步指令流, 确保寄存器配置立即生效
	isb

	// 组装 TCR_EL1 配置值（单行写法）
	ldr x0, =((0b101 << 32) | (0b00 << 36) | (1 << 40) | (1 << 41) | (0b00 << 30) | (0b10 << 28) | (0b01 << 26) | (0b01 << 24) | (16 << 16) | (0b00 << 14) | (0b10 << 12) | (0b01 << 10) | (0b01 << 8) | (32 << 0))

	msr tcr_el1, x0 // 写入 TCR_EL1
	isb             // 指令同步，确保配置立即生效

	// ... (前部分 MAIR/TCR/TTBR 配置保持不变) ...

	// --------------------------
	// 修正 1: 在开启 MMU 之前刷新 TLB
	// --------------------------
	tlbi vmalle1 // 刷新 TLB
	dsb  sy      // 确保 TLB 刷新完成
	isb          // 确保指令流同步

	// --------------------------
	// 3. 初始化栈指针 (致命修正)
	// --------------------------
	ldr x0, =el1_stack_top // 必须指向有效的 RAM 区域，且 16 字节对齐
	mov sp, x0

	// --------------------------
	// 2. 开启 MMU + 缓存
	// --------------------------
	mrs x0, sctlr_el1
	ldr x1, =~( (1 << 0) | (1 << 2) | (1 << 12) | (1 << 3) | (1 << 4) | (1 << 19) )
	and x0, x0, x1
	ldr x1, =( (1 << 0) | (1 << 2) | (1 << 12) | (1 << 3) | (1 << 4) | (1 << 19) )
	orr x0, x0, x1
	msr sctlr_el1, x0
	isb                                                                             // 确保 MMU 配置生效

	// --------------------------
	// 4. 调用 main 函数
	// --------------------------
	mov x0, #0
	mov x1, #0
	bl  main

	// 防止 main 返回后跑飞
	b .

halt:
	wfe
	b halt

