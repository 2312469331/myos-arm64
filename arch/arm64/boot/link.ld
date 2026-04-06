/* AArch64 三层架构 (EL3 → EL2 → EL1) 链接脚本
   命名规范: {EL层级}_{功能}_{类型}, 全大写常量, 层级绝对清晰
   已移除所有 AT() 语法，页表段不放在最前，仅通过 ALIGN() 控制布局
*/

/* linker.ld for Clang/LLD */
OUTPUT_FORMAT(elf64-littleaarch64)
ENTRY(_start) /* 入口点 */

/* 定义物理内存起始地址 */
PHYS_BASE = 0x40000000;
/* 定义虚拟内存高地址偏移 (Kernel Base) */
VIRT_BASE = 0xffff800000000000 + PHYS_BASE;
/* 方法1：直接基于 VIRT_BASE/PHYS_BASE 计算（简单直观） */
el1_stack_top_phys = el1_stack_top - 0xffff800000000000  ;
el2_stack_top_phys = el2_stack_top - 0xffff800000000000  ;
el3_stack_top_phys = el3_stack_top - 0xffff800000000000  ;
/* ==============================================
   LLD 规范版内核链接脚本
   特性：严格遵循 LLD 文档，无单位解析错误，覆盖核心语法
   适配：ARM64 内核/裸机镜像
   ============================================== */

/* ---------------- 1. 内存区域定义 (MEMORY) ---------------- */
/* 规范：ORIGIN/LENGTH 必须是纯数值，禁止使用 1G/2M 等单位 */
/* 物理内存基址 & 长度：0x40000000 开始，2MB = 2097152 字节 */
/* 虚拟内存基址 & 长度：高地址（内核虚拟空间），1GB = 1073741824 字节 */
MEMORY
{
  /* 物理内存 0x40000000 ~ 0x140000000，共 4GB */
    BOOT_PHYS (rwx) : ORIGIN = 0x40000000, LENGTH = 0x100000000
    /* 虚拟内存 0xFFFF000000000000 ~ 0xFFFF000100000000，共 4GB */
    KERNEL_VIRT (rwx) : ORIGIN = 0xFFFF800000000000 + PHYS_BASE, LENGTH = 0x100000000
}

/* ---------------- 2. 段分类 (CLASS) - 文档规范示例 ---------------- */
/* 规范：CLASS 定义输入段集合，不可嵌套，输入段仅归属一个CLASS */
SECTIONS
{
    /* ---------------- 3. 段布局核心 (SECTIONS 命令) ---------------- */
    /* 3.1 早期启动段：物理地址 1:1 映射 */
    .boot :
    {
        KEEP(*(.boot.text))  /* 保留启动汇编代码 */
        KEEP(*(.boot.data))  /* 保留早期数据 */

        . = ALIGN(4096);    /* 4K 对齐，符合 LLD 段对齐规则 */
    } > BOOT_PHYS

    /* 计算页表段的物理地址 */
    pagetable_lma = ADDR(.boot) + SIZEOF(.boot);

    /* 3.1.1 页表段：物理地址，用于 MMU 页表初始化 */
    .pagetable : AT(pagetable_lma)
    {
        KEEP(*(.pagetable))  /* 保留所有 .pagetable 输入段（页表数据） */
        . = ALIGN(4096);     /* 强制 4K 对齐，满足 ARM64 页表硬件要求 */
    } > BOOT_PHYS

    /* 计算栈段的物理地址 */
    stacks_lma = pagetable_lma + SIZEOF(.pagetable);
    stacks_vma = VIRT_BASE + (stacks_lma - PHYS_BASE);
    . = stacks_vma;
    /* 3.4 栈空间定义：内核异常栈 */
    /* 定义一个栈专用段 */
    .stacks stacks_vma : AT(stacks_lma)
    {
        . = ALIGN(16);
        . += 0x10;
        el3_stack_top = .;
        . += 0x80;
        el2_stack_top = .;
         . += 0x10000;
        el1_stack_top = .;
    }>KERNEL_VIRT  

    /* 计算内核段的物理地址和虚拟地址 */
    kernel_lma = stacks_lma + SIZEOF(.stacks);
    kernel_vma = VIRT_BASE + (kernel_lma - PHYS_BASE);
    . = kernel_vma;
    /* 3.2 内核核心段：虚拟地址(VMA) + 物理加载地址(LMA) 分离 */
    .kernel kernel_vma : AT(kernel_lma)
    {
        . = ALIGN(4096);
        /* 代码段：可执行，类型 SHT_PROGBITS */
        *(.text .text.*)
        *(.rodata .rodata.*)
        
        /* 数据段：可读写 */
        *(.data .data.*)
        *(.got .got.plt)  /* 包含全局偏移表 */
        *(.plt)          /* 包含过程链接表 */
        
        /* BSS 段：未初始化数据，SHT_NOBITS 类型 */
        . = ALIGN(16);
        __bss_start = .;    /* 符号赋值：定义BSS起始 */
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;      /* 符号赋值：定义BSS结束 */


    } >KERNEL_VIRT

    /* 3.3 废弃无用段：符合 LLD 规范 */
    /DISCARD/ : {
        *(.comment)   /* 去掉注释段 */
        *(.eh_frame)  /* 去掉异常处理帧 */
        *(.rel*)      /* 去掉重定位信息 */
    }

}
