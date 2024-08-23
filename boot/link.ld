ENTRY(_start)
SECTIONS {
    . = 0x40000000;

    /* 强制将 boot.S 的 .text 段放在最前面 */
    .text : {
        *(.text)           /* 再放其他代码 */
    }
    /* 🔥 精准匹配 Clang 生成的所有字符串段 */
    .rodata.str ALIGN(8) : {
        SUBALIGN(8)
        *(.rodata.str1.1)
        *(.rodata.str1.8)
        *(.rodata.cst8)  /* 匹配常量字符串段 */
    }

    .rodata ALIGN(16) : {
        *(.rodata)
        *(.rodata.*)
    }
     /* 已初始化数据段 */
     .data ALIGN(8) : {
         *(.data)
         *(.data.*)
     }
     /* 未初始化数据段（运行时由代码清零） */
     .bss ALIGN(8) : {
         *(COMMON)
         *(.bss)
         *(.bss.*)
     }
    . = ALIGN(16);
    . += 0x1000; /* 4KB 栈 */
    stack_top = .;
}

