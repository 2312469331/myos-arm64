/* AArch64 三层架构 (EL3 → EL2 → EL1) 链接脚本
   命名规范: {EL层级}_{功能}_{类型}, 全大写常量, 层级绝对清晰
   已移除所有 AT() 语法，页表段不放在最前，仅通过 ALIGN() 控制布局
*/

/* ---------------------- 1. 全局常量配置 (集中管理, 改这里就行) ---------------------- */
SECTION_ALIGN     = 8;            /* 段对齐: 8 字节 (AArch64 64位数据/指令对齐) */
STACK_ALIGN       = 16;           /* 栈对齐: 16 字节 (AAPCS64 强制要求, 函数调用必用) */

/* 各异常级别栈大小 (可按需调整, 单位: 字节) */
EL3_STACK_SIZE    = 0x2000;      /* EL3 (安全固件层) 栈: 8KB (比EL2/EL1大, 存早期初始化) */
EL2_STACK_SIZE    = 0x1000;      /* EL2 (虚拟化层) 栈: 4KB (存切换/虚拟化相关上下文) */
EL1_STACK_SIZE    = 0x1000;      /* EL1 (内核层) 栈: 4KB (存C代码/main/异常处理) */

/* linker.ld for Clang/LLD */
OUTPUT_FORMAT(elf64-littleaarch64)
ENTRY(_start) /* 入口点 */

/* 定义物理内存起始地址 */
PHYS_BASE = 0x40000000;
/* 定义虚拟内存高地址偏移 (Kernel Base) */
VIRT_BASE = 0xffff000000000000;

MEMORY
{
    /* 物理内存区域：从 0x40000000 开始 */
    /* 这里的地址是运行时地址(VMA)也是加载地址(LMA) */
    BOOT_PHYS (rwx) : ORIGIN = PHYS_BASE, LENGTH = 2M
    
    /* 
     * 内核虚拟区域：VMA设置为高虚拟地址
     * 但 AT(PHYS_BASE + 2M) 表示这段数据实际上加载在物理内存的 2M 偏移处
     */
    KERNEL_VIRT (rwx) : ORIGIN = VIRT_BASE, LENGTH = 1G
}

SECTIONS
{
    /* ================================================= */
    /* 1. 早期启动代码 (物理地址 1:1 映射)               */
    /* ================================================= */
    .boot :
    {
        KEEP(*(.boot.text))    /* 启动汇编代码 */
        KEEP(*(.boot.data))    /* 早期数据 */
        . = ALIGN(4096);
    } : BOOT_PHYS

    /* ================================================= */
    /* 2. MMU 开启后的代码 (虚拟地址)                    */
    /* 关键：VMA在高地址，LMA在紧接boot的物理地址        */
    /* ================================================= */
    .kernel :
    {
        /* 代码段 */
        *(.text .text.*)
        *(.rodata .rodata.*)
        
        /* 数据段 */
        *(.data .data.*)
        
        /* BSS段 */
        . = ALIGN(16);
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } : KERNEL_VIRT AT(BOOT_PHYS) /* AT 指定加载地址 */

    /* 丢弃无用段 */
    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
    }
}
    /* ---------------------- 栈空间 (不变, 保持原样) ---------------------- */
    . = ALIGN(STACK_ALIGN);

    . += EL3_STACK_SIZE;
    el3_stack_top = .;

    . += EL2_STACK_SIZE;
    el2_stack_top = .;

    . += EL1_STACK_SIZE;
    el1_stack_top = .;
}

