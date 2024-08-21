ENTRY(_start)
SECTIONS {
    . = 0x40000000;

    /* 强制将 boot.S 的 .text 段放在最前面 */
    .text : {
        *(.text)           /* 再放其他代码 */
    }
    /* 只读数据段（字符串等），紧跟在 .text 之后，8字节对齐 */
     .rodata ALIGN(8) : {
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
    . = ALIGN(8);
    . += 0x1000; /* 4KB 栈 */
    stack_top = .;
}

