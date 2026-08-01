.text
pick:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $256, %rsp
	movq %rdi, -176(%rbp)
	movq %rsi, -168(%rbp)
	movq %rdx, -160(%rbp)
	movq %rcx, -152(%rbp)
	movq %r8, -144(%rbp)
	movq %r9, -136(%rbp)
	movaps %xmm0, -128(%rbp)
	movaps %xmm1, -112(%rbp)
	movaps %xmm2, -96(%rbp)
	movaps %xmm3, -80(%rbp)
	movaps %xmm4, -64(%rbp)
	movaps %xmm5, -48(%rbp)
	movaps %xmm6, -32(%rbp)
	movaps %xmm7, -16(%rbp)
	movl $8, -256(%rbp)
	movl $48, -252(%rbp)
	movq %rbp, %rax
	addq $16, %rax
	movq %rax, -248(%rbp)
	movq %rbp, %rax
	addq $-176, %rax
	movq %rax, -240(%rbp)
	movslq -256(%rbp), %rcx
	cmpl $48, %ecx
	jb .Lbb2
	movq -248(%rbp), %rax
	movq %rax, %rcx
	addq $8, %rcx
	movq %rcx, -248(%rbp)
	jmp .Lbb3
.Lbb2:
	movq -240(%rbp), %rax
	addq %rcx, %rax
	addl $8, %ecx
	movl %ecx, -256(%rbp)
.Lbb3:
	movl (%rax), %ecx
	movslq -252(%rbp), %rdx
	cmpl $176, %edx
	jb .Lbb5
	movq -248(%rbp), %rax
	movq %rax, %rdx
	addq $8, %rdx
	movq %rdx, -248(%rbp)
	jmp .Lbb6
.Lbb5:
	movq -240(%rbp), %rax
	addq %rdx, %rax
	addl $16, %edx
	movl %edx, -252(%rbp)
.Lbb6:
	movsd (%rax), %xmm0
	movslq -256(%rbp), %rdx
	cmpl $48, %edx
	jb .Lbb8
	movq -248(%rbp), %rax
	movq %rax, %rdx
	addq $8, %rdx
	movq %rdx, -248(%rbp)
	jmp .Lbb9
.Lbb8:
	movq -240(%rbp), %rax
	addq %rdx, %rax
	addl $8, %edx
	movl %edx, -256(%rbp)
.Lbb9:
	movq (%rax), %rax
	cmpl $7, %ecx
	jnz .Lbb11
	ucomisd ".Lfp0"(%rip), %xmm0
	setz %cl
	movzbl %cl, %ecx
	setnp %dl
	movzbl %dl, %edx
	andl %edx, %ecx
	jnz .Lbb12
.Lbb11:
	movl $0, %eax
.Lbb12:
	leave
	ret
.type pick, @function
.size pick, .-pick
.data
.balign 1
.Lstring.3:
	.ascii "value\000"
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	movl $9, %edx
	movsd ".Lfp0"(%rip), %xmm0
	movl $7, %esi
	leaq .Lstring.3(%rip), %rdi
	callq pick
	movq %rax, -16(%rbp)
	cmpq $9, %rax
	movl $1, %ecx
	movl $0, %eax
	cmovnz %ecx, %eax
	leave
	ret
.type main, @function
.size main, .-main
/* floating point constants */
.section .rodata
.p2align 3
.Lfp0:
	.quad 4612811918334230528 /* 2.500000 */

.section .note.GNU-stack,"",@progbits
