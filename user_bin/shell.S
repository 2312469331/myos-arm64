.section .text
.globl _start

_start:
    mov x8, #0
    adr x1, msg_hello
    mov x2, #20
    svc #0

loop:
    mov x8, #0
    adr x1, prompt
    mov x2, #9
    svc #0

    sub sp, sp, #256
    mov x8, #9
    mov x0, #0
    mov x1, sp
    mov x2, #240
    svc #0
    mov x19, x0

    subs x19, x19, #1
    b.lt empty
    ldrb w20, [sp, x19]
    cmp w20, #10
    b.ne proc_cmd
    strb wzr, [sp, x19]
    cbz x19, empty

proc_cmd:
    adr x0, cmd_echo
    mov x1, sp
    mov x2, #4
    bl strncmp
    cbnz x0, try_run
    subs x2, x19, #5
    b.lt cmd_done
    add x1, sp, #5
    mov x8, #10
    mov x0, #1
    svc #0
    mov x8, #10
    mov x0, #1
    adr x1, newline
    mov x2, #1
    svc #0
    b cmd_done

try_run:
    mov x8, #6
    svc #0
    cbnz x0, parent_wait

    mov x0, sp
    mov x1, #0
    mov x8, #15
    svc #0
    mov x8, #0
    adr x1, msg_nfound
    mov x2, #18
    svc #0
    mov x8, #1
    svc #0

parent_wait:
    mov x8, #7
    svc #0
    b cmd_done

empty:
    b cmd_done

cmd_done:
    add sp, sp, #256
    b loop

strncmp:
    stp x3, x4, [sp, #-16]!
    mov x3, #0
1:
    cmp x3, x2
    b.ge 2f
    ldrb w4, [x0, x3]
    ldrb w5, [x1, x3]
    cmp w4, w5
    b.ne 3f
    cbz w4, 2f
    add x3, x3, #1
    b 1b
2:
    mov x0, #0
    ldp x3, x4, [sp], #16
    ret
3:
    mov x0, #1
    ldp x3, x4, [sp], #16
    ret

.align 4
msg_hello: .ascii "shell v0.3\n"
.align 4
prompt: .ascii "myos:~$ "
.align 4
cmd_echo: .ascii "echo"
.align 4
newline: .ascii "\n"
.align 4
msg_nfound: .ascii "command not found\n"
