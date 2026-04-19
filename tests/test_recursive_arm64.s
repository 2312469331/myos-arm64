	.arch armv8-a
	.file	"test_recursive_detailed.c"
	.text
	.align	2
	.global	recursive_func
	.type	recursive_func, %function
recursive_func:
.LFB0:
	.cfi_startproc
	stp	x29, x30, [sp, -48]!
	.cfi_def_cfa_offset 48
	.cfi_offset 29, -48
	.cfi_offset 30, -40
	mov	x29, sp
	str	w0, [sp, 28]
	str	w1, [sp, 24]
	ldr	w0, [sp, 28]
	cmp	w0, 0
	bgt	.L2
	ldr	w0, [sp, 24]
	b	.L3
.L2:
	ldr	w0, [sp, 28]
	sub	w2, w0, #1
	ldr	w0, [sp, 24]
	lsl	w0, w0, 1
	mov	w1, w0
	mov	w0, w2
	bl	recursive_func
	str	w0, [sp, 44]
	ldr	w1, [sp, 44]
	ldr	w0, [sp, 28]
	add	w0, w1, w0
.L3:
	ldp	x29, x30, [sp], 48
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa_offset 0
	ret
	.cfi_endproc
.LFE0:
	.size	recursive_func, .-recursive_func
	.align	2
	.global	complex_recursive
	.type	complex_recursive, %function
complex_recursive:
.LFB1:
	.cfi_startproc
	stp	x29, x30, [sp, -48]!
	.cfi_def_cfa_offset 48
	.cfi_offset 29, -48
	.cfi_offset 30, -40
	mov	x29, sp
	str	w0, [sp, 28]
	str	w1, [sp, 24]
	str	w2, [sp, 20]
	str	w3, [sp, 16]
	ldr	w0, [sp, 16]
	cmp	w0, 0
	bgt	.L5
	ldr	w1, [sp, 28]
	ldr	w0, [sp, 24]
	add	w1, w1, w0
	ldr	w0, [sp, 20]
	add	w0, w1, w0
	b	.L6
.L5:
	ldr	w0, [sp, 28]
	sub	w4, w0, #1
	ldr	w0, [sp, 16]
	sub	w0, w0, #1
	mov	w3, w0
	ldr	w2, [sp, 20]
	ldr	w1, [sp, 24]
	mov	w0, w4
	bl	complex_recursive
	str	w0, [sp, 40]
	ldr	w0, [sp, 24]
	sub	w1, w0, #1
	ldr	w0, [sp, 16]
	sub	w0, w0, #1
	mov	w3, w0
	ldr	w2, [sp, 20]
	ldr	w0, [sp, 28]
	bl	complex_recursive
	str	w0, [sp, 44]
	ldr	w1, [sp, 40]
	ldr	w0, [sp, 44]
	add	w1, w1, w0
	ldr	w0, [sp, 28]
	add	w1, w1, w0
	ldr	w0, [sp, 24]
	add	w1, w1, w0
	ldr	w0, [sp, 20]
	add	w0, w1, w0
.L6:
	ldp	x29, x30, [sp], 48
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa_offset 0
	ret
	.cfi_endproc
.LFE1:
	.size	complex_recursive, .-complex_recursive
	.align	2
	.global	test_external_call
	.type	test_external_call, %function
test_external_call:
.LFB2:
	.cfi_startproc
	stp	x29, x30, [sp, -48]!
	.cfi_def_cfa_offset 48
	.cfi_offset 29, -48
	.cfi_offset 30, -40
	mov	x29, sp
	str	w0, [sp, 28]
	str	w1, [sp, 24]
	str	w2, [sp, 20]
	mov	w5, 3
	mov	w4, 2
	mov	w3, 1
	ldr	w2, [sp, 20]
	ldr	w1, [sp, 24]
	ldr	w0, [sp, 28]
	bl	external_func
	ldr	w1, [sp, 28]
	ldr	w0, [sp, 24]
	add	w0, w1, w0
	ldr	w1, [sp, 20]
	add	w0, w1, w0
	str	w0, [sp, 44]
	mov	w5, 2
	mov	w4, 1
	ldr	w3, [sp, 20]
	ldr	w2, [sp, 24]
	ldr	w1, [sp, 28]
	ldr	w0, [sp, 44]
	bl	external_func
	nop
	ldp	x29, x30, [sp], 48
	.cfi_restore 30
	.cfi_restore 29
	.cfi_def_cfa_offset 0
	ret
	.cfi_endproc
.LFE2:
	.size	test_external_call, .-test_external_call
	.ident	"GCC: (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0"
	.section	.note.GNU-stack,"",@progbits
