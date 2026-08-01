.text
mk:
	endbr64
	subq $56, %rsp
	movq %rdi, %rax
	movq %rax, 32(%rsp)
	movl $1, 0(%rsp)
	movl $2, 4(%rsp)
	movl $3, 8(%rsp)
	movl $0, 12(%rsp)
	movl $0, 16(%rsp)
	movl $1074790400, 20(%rsp)
	movq 0(%rsp), %rcx
	movq %rcx, 0(%rax)
	movq 8(%rsp), %rcx
	movq %rcx, 8(%rax)
	movq 16(%rsp), %rcx
	movq %rcx, 16(%rax)
	addq $56, %rsp
	ret
.type mk, @function
.size mk, .-mk
.text
chk:
	endbr64
	movl 8(%rsp), %eax
	movl 12(%rsp), %ecx
	addl %ecx, %eax
	movl 16(%rsp), %ecx
	addl %ecx, %eax
	cmpl $6, %eax
	jnz .Lbb4
	movsd 24(%rsp), %xmm0
	ucomisd ".Lfp0"(%rip), %xmm0
	setz %al
	movzbl %al, %eax
	setnp %cl
	movzbl %cl, %ecx
	andl %ecx, %eax
	jnz .Lbb5
.Lbb4:
	movl $1, %eax
	jmp .Lbb6
.Lbb5:
	movl $0, %eax
.Lbb6:
	ret
.type chk, @function
.size chk, .-chk
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $48, %rsp
	leaq -40(%rbp), %rdi
	callq mk
	movq %rax, -8(%rbp)
	subq $32, %rsp
	movq %rsp, %rcx
	movq 0(%rax), %rdx
	movq %rdx, 0(%rcx)
	movq 8(%rax), %rdx
	movq %rdx, 8(%rcx)
	movq 16(%rax), %rax
	movq %rax, 16(%rcx)
	callq chk
	movl %eax, -16(%rbp)
	subq $-32, %rsp
	leave
	ret
.type main, @function
.size main, .-main
/* floating point constants */
.section .rodata
.p2align 3
.Lfp0:
	.quad 4616189618054758400 /* 4.000000 */

.section .note.GNU-stack,"",@progbits
