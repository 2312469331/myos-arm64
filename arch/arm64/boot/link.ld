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
VIRT_BASE = 0xffff000000000000;
/* 方法1：直接基于 VIRT_BASE/PHYS_BASE 计算（简单直观） */
     el1_stack_top_phys = el1_stack_top - (VIRT_BASE - PHYS_BASE);
el2_stack_top_phys = el2_stack_top - (VIRT_BASE - PHYS_BASE);
el3_stack_top_phys = el3_stack_top - (VIRT_BASE - PHYS_BASE);
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
    BOOT_PHYS (rwx) : ORIGIN = 0x40000000, LENGTH = 2097152
  KERNEL_VIRT (rwx) : ORIGIN = 0xFFFF000000000000, LENGTH = 1073741824
}

/* ---------------- 2. 段分类 (CLASS) - 文档规范示例 ---------------- */
/* 规范：CLASS 定义输入段集合，不可嵌套，输入段仅归属一个CLASS */
SECTIONS
{
    /* 定义 rodata 子段分类：将 .rodata.early 归类为 c 类 */
    CLASS(c) { *(.rodata.early) }

    /* ---------------- 3. 段布局核心 (SECTIONS 命令) ---------------- */
    /* 3.1 早期启动段：物理地址 1:1 映射 */
    .boot :
    {
        KEEP(*(.boot.text))  /* 保留启动汇编代码 */
        KEEP(*(.boot.data))  /* 保留早期数据 */
        . = ALIGN(4096);    /* 4K 对齐，符合 LLD 段对齐规则 */
        /* 记录.kernel 物理地址相对于 PHYS_BASE 的偏移 */
    } > BOOT_PHYS
/* 3.1.1 页表段：物理地址，用于 MMU 页表初始化 */
     .pagetable :
     {
         KEEP(*(.pagetable))  /* 保留所有 .pagetable 输入段（页表数据） */
         . = ALIGN(4096);     /* 强制 4K 对齐，满足 ARM64 页表硬件要求 */

     } > BOOT_PHYS  /* 与 .boot 同物理内存区域，MMU 开启前可直接访问物理地址 */
     /*   . = ALIGN(4096); */
     /* kernel_phys_offset = . - PHYS_BASE; */
    /* 3.2 内核核心段：虚拟地址(VMA) + 物理加载地址(LMA) 分离 */
    /* 规范：AT(lma) 指定加载地址，VMA 为 KERNEL_VIRT 虚拟地址 */
     /* 关键：这里手动让 .kernel 段的 VMA 也跟着物理地址的偏移走 */
     /* 关键：记录当前物理地址（.kernel 段的 LMA 起始） */
     __kernel_lma_start = .;
     /* 计算物理地址相对于 PHYS_BASE 的偏移 */
     __kernel_offset = __kernel_lma_start - PHYS_BASE;
     /* 虚拟地址 = 虚拟基地址 + 物理偏移，LLD 允许这种符号计算 */
     __kernel_vma_start = VIRT_BASE + __kernel_offset;
     .kernel __kernel_vma_start : AT(__kernel_lma_start)
    {
        . = ALIGN(4096);
        /* 代码段：可执行，类型 SHT_PROGBITS */
        *(.text .text.*)
        *(.rodata .rodata.*) CLASS(c) /* 引用之前定义的 c 类段 */
        
        /* 数据段：可读写 */
        *(.data .data.*)
        
        /* BSS 段：未初始化数据，SHT_NOBITS 类型 */
        . = ALIGN(16);
        __bss_start = .;    /* 符号赋值：定义BSS起始 */
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;      /* 符号赋值：定义BSS结束 */
    }>KERNEL_VIRT  /* VMA在高地址，LMA在BOOT_PHYS区域 */

    /* 3.3 废弃无用段：符合 LLD 规范 */
    /DISCARD/ : {
        *(.comment)   /* 去掉注释段 */
        *(.eh_frame)  /* 去掉异常处理帧 */
    }

    /* 3.4 栈空间定义：内核异常栈 */
    /* 定义一个栈专用段 */
.stacks (NOLOAD) : {
    . = ALIGN(16);
    el3_stack_top = .;
    . += 0x10000;
    el2_stack_top = .;
    . += 0x8000;
    el1_stack_top = .;
    . += 0x4000;
} > KERNEL_VIRT

}

/* ---------------- 5. 内置函数示例 (DATA_SEGMENT_RELRO_END) ---------------- */
/* 说明：内核开发通常无需 RELRO，此处仅展示 LLD 规范用法 */
/* 仅在启用 -z relro 时生效，自动对齐到最大页边界 */
/* . = DATA_SEGMENT_RELRO_END(0, .); */

/* ---------------- 6. 符号赋值规范 ---------------- */
/* 规范：= 定义新符号，+= 操作已有符号；算术运算会重置 st_type */
__kernel_start = LOADADDR(.kernel); /* 获取内核加载地址 */
__kernel_size = __bss_end - __kernel_start; /* 计算内核总大小 */

