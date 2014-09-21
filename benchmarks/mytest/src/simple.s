	.file	"simple.bc"
	.text
	.globl	main
	.align	16, 0x90
	.type	main,@function
main:                                   # @main
	.cfi_startproc
# BB#0:
	pushq	%rbp
.Ltmp2:
	.cfi_def_cfa_offset 16
.Ltmp3:
	.cfi_offset %rbp, -16
	movq	%rsp, %rbp
.Ltmp4:
	.cfi_def_cfa_register %rbp
	subq	$4032, %rsp             # imm = 0xFC0
	movl	$0, -4(%rbp)
	movl	$0, -4020(%rbp)
	jmp	.LBB0_1
	.align	16, 0x90
.LBB0_2:                                #   in Loop: Header=BB0_1 Depth=1
	movslq	-4020(%rbp), %rax
	movl	$0, -4016(%rbp,%rax,4)
	incl	-4020(%rbp)
.LBB0_1:                                # =>This Inner Loop Header: Depth=1
	cmpl	$999, -4020(%rbp)       # imm = 0x3E7
	jle	.LBB0_2
# BB#3:
	movl	$100, -4024(%rbp)
	jmp	.LBB0_4
	.align	16, 0x90
.LBB0_5:                                #   in Loop: Header=BB0_4 Depth=1
	movslq	-4024(%rbp), %rax
	addl	$10, -4016(%rbp,%rax,4)
	incl	-4024(%rbp)
.LBB0_4:                                # =>This Inner Loop Header: Depth=1
	cmpl	$999, -4024(%rbp)       # imm = 0x3E7
	jle	.LBB0_5
# BB#6:
	movl	$0, -4020(%rbp)
	jmp	.LBB0_7
	.align	16, 0x90
.LBB0_8:                                #   in Loop: Header=BB0_7 Depth=1
	movq	stdout(%rip), %rdi
	movslq	-4020(%rbp), %rax
	movl	-4016(%rbp,%rax,4), %edx
	movl	$.L.str, %esi
	xorl	%eax, %eax
	callq	fprintf
	incl	-4020(%rbp)
.LBB0_7:                                # =>This Inner Loop Header: Depth=1
	cmpl	$999, -4020(%rbp)       # imm = 0x3E7
	jle	.LBB0_8
# BB#9:
	movl	$1, %eax
	addq	$4032, %rsp             # imm = 0xFC0
	popq	%rbp
	ret
.Ltmp5:
	.size	main, .Ltmp5-main
	.cfi_endproc

	.type	.L.str,@object          # @.str
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str:
	.asciz	"%d\n"
	.size	.L.str, 4


	.section	".note.GNU-stack","",@progbits
