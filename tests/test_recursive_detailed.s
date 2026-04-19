	.file	"test_recursive_detailed.c"
	.text
	.globl	recursive_func
	.type	recursive_func, @function
recursive_func:
.LFB0:
	.cfi_startproc
	endbr64
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$32, %rsp
	movl	%edi, -20(%rbp)
	movl	%esi, -24(%rbp)
	cmpl	$0, -20(%rbp)
	jg	.L2
	movl	-24(%rbp), %eax
	jmp	.L3
.L2:
	movl	-24(%rbp), %eax
	leal	(%rax,%rax), %edx
	movl	-20(%rbp), %eax
	subl	$1, %eax
	movl	%edx, %esi
	movl	%eax, %edi
	call	recursive_func
	movl	%eax, -4(%rbp)
	movl	-4(%rbp), %edx
	movl	-20(%rbp), %eax
	addl	%edx, %eax
.L3:
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE0:
	.size	recursive_func, .-recursive_func
	.globl	complex_recursive
	.type	complex_recursive, @function
complex_recursive:
.LFB1:
	.cfi_startproc
	endbr64
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$32, %rsp
	movl	%edi, -20(%rbp)
	movl	%esi, -24(%rbp)
	movl	%edx, -28(%rbp)
	movl	%ecx, -32(%rbp)
	cmpl	$0, -32(%rbp)
	jg	.L5
	movl	-20(%rbp), %edx
	movl	-24(%rbp), %eax
	addl	%eax, %edx
	movl	-28(%rbp), %eax
	addl	%edx, %eax
	jmp	.L6
.L5:
	movl	-32(%rbp), %eax
	leal	-1(%rax), %ecx
	movl	-20(%rbp), %eax
	leal	-1(%rax), %edi
	movl	-28(%rbp), %edx
	movl	-24(%rbp), %eax
	movl	%eax, %esi
	call	complex_recursive
	movl	%eax, -8(%rbp)
	movl	-32(%rbp), %eax
	leal	-1(%rax), %ecx
	movl	-24(%rbp), %eax
	leal	-1(%rax), %esi
	movl	-28(%rbp), %edx
	movl	-20(%rbp), %eax
	movl	%eax, %edi
	call	complex_recursive
	movl	%eax, -4(%rbp)
	movl	-8(%rbp), %edx
	movl	-4(%rbp), %eax
	addl	%eax, %edx
	movl	-20(%rbp), %eax
	addl	%eax, %edx
	movl	-24(%rbp), %eax
	addl	%eax, %edx
	movl	-28(%rbp), %eax
	addl	%edx, %eax
.L6:
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE1:
	.size	complex_recursive, .-complex_recursive
	.globl	test_external_call
	.type	test_external_call, @function
test_external_call:
.LFB2:
	.cfi_startproc
	endbr64
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset 6, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register 6
	subq	$32, %rsp
	movl	%edi, -20(%rbp)
	movl	%esi, -24(%rbp)
	movl	%edx, -28(%rbp)
	movl	-28(%rbp), %edx
	movl	-24(%rbp), %esi
	movl	-20(%rbp), %eax
	movl	$3, %r9d
	movl	$2, %r8d
	movl	$1, %ecx
	movl	%eax, %edi
	call	external_func@PLT
	movl	-20(%rbp), %edx
	movl	-24(%rbp), %eax
	addl	%eax, %edx
	movl	-28(%rbp), %eax
	addl	%edx, %eax
	movl	%eax, -4(%rbp)
	movl	-28(%rbp), %ecx
	movl	-24(%rbp), %edx
	movl	-20(%rbp), %esi
	movl	-4(%rbp), %eax
	movl	$2, %r9d
	movl	$1, %r8d
	movl	%eax, %edi
	call	external_func@PLT
	nop
	leave
	.cfi_def_cfa 7, 8
	ret
	.cfi_endproc
.LFE2:
	.size	test_external_call, .-test_external_call
	.ident	"GCC: (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0"
	.section	.note.GNU-stack,"",@progbits
	.section	.note.gnu.property,"a"
	.align 8
	.long	1f - 0f
	.long	4f - 1f
	.long	5
0:
	.string	"GNU"
1:
	.align 8
	.long	0xc0000002
	.long	3f - 2f
2:
	.long	0x3
3:
	.align 8
4:
