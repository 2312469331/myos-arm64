/* ==================================================
   🌟 AArch64 三层架构（EL3 → EL2 → EL1）链接脚本
   命名规范：{EL层级}_{功能}_{类型}，全大写常量，层级绝对清晰
   ================================================== */

/* -------------------------- 1. 全局常量配置（集中管理，改这里就行） -------------------------- */
SECTION_ALIGN       = 8;           /* 段对齐：8 字节（AArch64 64位数据/指令对齐） */
STACK_ALIGN         = 16;          /* 栈对齐：16 字节（AAPCS64 强制要求，函数调用必用） */

/* 各异常级别栈大小（可按需调整，单位：字节） */
EL3_STACK_SIZE      = 0x2000;      /* EL3（安全固件层）栈：8KB（比EL2/EL1大，存早期初始化） */
EL2_STACK_SIZE      = 0x1000;      /* EL2（虚拟化层）栈：4KB（存切换/虚拟化相关上下文） */
EL1_STACK_SIZE      = 0x1000;      /* EL1（内核层）栈：4KB（存C代码/main/异常处理） */
/*
  * 极简说明：
  * PHYS:  内核被加载到的物理地址 (QEMU virt 标准)
  * VIRT:  内核正式运行的高虚拟地址 (抄 Linux 标准偏移)
  * PAGE_SIZE: 4KB 对齐
  */
 PHYS_BASE    = 0x40000000;
 VIRT_BASE    = 0xFFFF800040080000;
 PAGE_SIZE    = 4096;
 ENTRY(_start);
/* 入口点：必须对应汇编里的 _start 标签（EL3 启动入口） */
ENTRY(_start)

/* -------------------------- 2. 段布局与内存映射（按EL层级+标准段顺序） -------------------------- */
SECTIONS {
  /* 第一部分：物理地址段 —— 开机就能跑，不开MMU也能跑
      * 放：boot.S、建页表函数、早期数据、页表本身
      * ====================================================== */
     . = PHYS_BASE;
     _phys_start = .;
     /* 早期启动代码：物理地址 */
     .text.boot ALIGN(PAGE_SIZE) : {
         *(.boot)                /* boot.S */
         *(.text.boot)           /* 早期C函数：create_linear_mapping 这类 */
     }
     /* 早期数据 */
     .data.boot : {
         *(.data.boot)
     }
     /* 早期BSS */
     .bss.boot : {
         _bss_boot_start = .;
         *(.bss.boot)
         _bss_boot_end = .;
     }
     /* 页表必须4K对齐，放物理段 */
     .pagetable ALIGN(PAGE_SIZE) : {
         *(.pagetable)
     }
     /* ======================================================
    /* 定位内核基地址 */
    . = VIRT_BASE;

    /* -------------------------- 代码段（EL3 启动代码 + 内核代码） -------------------------- */
    /* 强制EL3启动代码（boot.S）优先放在最前，保证跳转正确 */
    .text ALIGN(SECTION_ALIGN) : {
        *(.text.boot)    /* 优先链接 boot.S 的 .text.boot 段（EL3 入口必须在最前） */
        *(.text)         /* 其他C/汇编代码段 */
        *(.text.*)       /* 匹配Clang生成的子段（如.text.main/.text.irq） */
    }

    /* -------------------------- 只读数据段（常量/字符串/向量表） -------------------------- */
    .rodata ALIGN(SECTION_ALIGN) : {
        *(.rodata)
        *(.rodata.*)
        *(.vectors)      /* 预留异常向量表段（后续可单独放EL3/EL2/EL1向量表） */
    }

    /* -------------------------- 已初始化数据段（全局变量/常量） -------------------------- */
    .data ALIGN(SECTION_ALIGN) : {
        *(.data)
        *(.data.*)
    }

    /* -------------------------- 未初始化数据段（BSS，运行时清零） -------------------------- */
    .bss ALIGN(SECTION_ALIGN) : {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }
    /* 页表段：4KB 对齐 */
     .pagetable : ALIGN(4096) {
         *(.pagetable)  // 存放 pgd/pud 数组
     }
    /* -------------------------- 3. 栈空间分配（严格按 EL3 → EL2 → EL1 顺序，栈向下生长） -------------------------- */
    /* 先做16字节对齐，保证所有栈符合AAPCS64规范 */
    . = ALIGN(STACK_ALIGN);

    /* 🔥 EL3 栈（最高优先级，先分配，地址最高） */
    . += EL3_STACK_SIZE;
    el3_stack_top = .;  /* EL3 栈顶符号（汇编中用：msr SP_EL3, x0） */

    /* 🔥 EL2 栈（中间层级，次高地址） */
    . += EL2_STACK_SIZE;
    el2_stack_top = .;  /* EL2 栈顶符号（汇编中用：msr SP_EL2, x0） */

    /* 🔥 EL1 栈（内核层，最低地址） */
    . += EL1_STACK_SIZE;
    el1_stack_top = .;  /* EL1 栈顶符号（汇编中用：msr SP_EL1, x0 / adrp x0, el1_stack_top） */
}
