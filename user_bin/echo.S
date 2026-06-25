.section .text
.globl _start
_start:
    mov x8, #64    // SYS_WRITE
    mov x0, #1     // fd = stdout
    adr x1, msg
    mov x2, #24
    svc #0
    mov x8, #93    // SYS_EXIT
    svc #0
.align 4
msg: .ascii "Hello from ECHO.ELF!\n"
