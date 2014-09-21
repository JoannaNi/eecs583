	.file	"simple.profile.bc"
	.text
	.globl	main
	.align	16, 0x90
	.type	main,@function
main:                                   # @main
	.cfi_startproc
# BB#0:                                 # %entry
	pushq	%rbp
.Ltmp2:
	.cfi_def_cfa_offset 16
.Ltmp3:
	.cfi_offset %rbp, -16
	movq	%rsp, %rbp
.Ltmp4:
	.cfi_def_cfa_register %rbp
	subq	$4048, %rsp             # imm = 0xFD0
	xorl	%edi, %edi
	xorl	%esi, %esi
	movl	$EdgeProfCounters, %edx
	movl	$25, %ecx
	callq	llvm_start_edge_profiling
	incl	EdgeProfCounters(%rip)
	movl	$0, -4(%rbp)
	movl	$0, -4020(%rbp)
	incl	EdgeProfCounters+4(%rip)
	jmp	.LBB0_1
	.align	16, 0x90
.LBB0_2:                                # %for.body
                                        #   in Loop: Header=BB0_1 Depth=1
	incl	EdgeProfCounters+8(%rip)
	movslq	-4020(%rbp), %rax
	movl	$0, -4016(%rbp,%rax,4)
	incl	EdgeProfCounters+16(%rip)
	incl	-4020(%rbp)
	incl	EdgeProfCounters+20(%rip)
.LBB0_1:                                # %for.cond
                                        # =>This Inner Loop Header: Depth=1
	cmpl	$999, -4020(%rbp)       # imm = 0x3E7
	jle	.LBB0_2
# BB#3:                                 # %for.end
	incl	EdgeProfCounters+12(%rip)
	movl	$100, -4024(%rbp)
	incl	EdgeProfCounters+24(%rip)
	jmp	.LBB0_4
	.align	16, 0x90
.LBB0_5:                                # %for.body3
                                        #   in Loop: Header=BB0_4 Depth=1
	incl	EdgeProfCounters+28(%rip)
	movslq	-4024(%rbp), %rax
	addl	$10, -4016(%rbp,%rax,4)
	incl	EdgeProfCounters+36(%rip)
	incl	-4024(%rbp)
	incl	EdgeProfCounters+40(%rip)
.LBB0_4:                                # %for.cond1
                                        # =>This Inner Loop Header: Depth=1
	cmpl	$999, -4024(%rbp)       # imm = 0x3E7
	jle	.LBB0_5
# BB#6:                                 # %for.end8
	incl	EdgeProfCounters+32(%rip)
	movl	$0, -4028(%rbp)
	incl	EdgeProfCounters+44(%rip)
	jmp	.LBB0_7
	.align	16, 0x90
.LBB0_11:                               # %if.end
                                        #   in Loop: Header=BB0_7 Depth=1
	incl	EdgeProfCounters+72(%rip)
	incl	-4028(%rbp)
	incl	EdgeProfCounters+76(%rip)
.LBB0_7:                                # %for.cond9
                                        # =>This Inner Loop Header: Depth=1
	cmpl	$999, -4028(%rbp)       # imm = 0x3E7
	jg	.LBB0_12
# BB#8:                                 # %for.body11
                                        #   in Loop: Header=BB0_7 Depth=1
	incl	EdgeProfCounters+48(%rip)
	movl	-4028(%rbp), %eax
	movl	%eax, %ecx
	shrl	$31, %ecx
	addl	%eax, %ecx
	andl	$-2, %ecx
	cmpl	%ecx, %eax
	jne	.LBB0_10
# BB#9:                                 # %if.then
                                        #   in Loop: Header=BB0_7 Depth=1
	incl	EdgeProfCounters+56(%rip)
	movslq	-4028(%rbp), %rax
	movl	$0, -4016(%rbp,%rax,4)
	incl	EdgeProfCounters+64(%rip)
	jmp	.LBB0_11
	.align	16, 0x90
.LBB0_10:                               # %if.else
                                        #   in Loop: Header=BB0_7 Depth=1
	incl	EdgeProfCounters+60(%rip)
	movslq	-4028(%rbp), %rax
	movl	$1, -4016(%rbp,%rax,4)
	incl	EdgeProfCounters+68(%rip)
	jmp	.LBB0_11
.LBB0_12:                               # %for.end19
	incl	EdgeProfCounters+52(%rip)
	movl	$0, -4020(%rbp)
	incl	EdgeProfCounters+80(%rip)
	jmp	.LBB0_13
	.align	16, 0x90
.LBB0_14:                               # %for.body22
                                        #   in Loop: Header=BB0_13 Depth=1
	incl	EdgeProfCounters+84(%rip)
	movslq	-4020(%rbp), %rax
	movl	-4016(%rbp,%rax,4), %edx
	movq	stdout(%rip), %rdi
	movl	$.L.str, %esi
	xorb	%al, %al
	callq	fprintf
	incl	EdgeProfCounters+92(%rip)
	incl	-4020(%rbp)
	incl	EdgeProfCounters+96(%rip)
.LBB0_13:                               # %for.cond20
                                        # =>This Inner Loop Header: Depth=1
	cmpl	$999, -4020(%rbp)       # imm = 0x3E7
	jle	.LBB0_14
# BB#15:                                # %for.end27
	incl	EdgeProfCounters+88(%rip)
	movl	$1, %eax
	addq	$4048, %rsp             # imm = 0xFD0
	popq	%rbp
	ret
.Ltmp5:
	.size	main, .Ltmp5-main
	.cfi_endproc

	.type	.L.str,@object          # @.str
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str:
	.asciz	 "%d\n"
	.size	.L.str, 4

	.type	EdgeProfCounters,@object # @EdgeProfCounters
	.local	EdgeProfCounters
	.comm	EdgeProfCounters,100,16

	.section	".note.GNU-stack","",@progbits
