ENTRY(_start)
SECTIONS {
    . = 0x40000000;

    /* 强制将 boot.S 的 .text 段放在最前面 */
    .text : {
        *(.text)           /* 再放其他代码 */
    }

    .rodata : { *(.rodata) }
    .data : { *(.data) }
    .bss : { *(.bss COMMON) }

    . = ALIGN(8);
    . += 0x1000; /* 4KB 栈 */
    stack_top = .;
}

