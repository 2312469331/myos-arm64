.global _start
_start:
    adrp x0, stack_top
    add  x0, x0, :lo12:stack_top
    mov sp, x0
    bl main  /* 确保 C 语言入口函数是 main */
    b .

