.text
.globl Vec_setii
Vec_setii:
	endbr64
	subq $16, %rsp
	movl %edx, 12(%rsp)
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl %esi, (%rdi)
	movl %edx, 4(%rdi)
	addq $16, %rsp
	ret
.type Vec_setii, @function
.size Vec_setii, .-Vec_setii
.text
.globl Vec_get
Vec_get:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl 4(%rdi), %eax
	addq $16, %rsp
	ret
.type Vec_get, @function
.size Vec_get, .-Vec_get
.text
.globl Guard_Guard
Guard_Guard:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $1, (%rdi)
	movl $2, 4(%rdi)
	addq $16, %rsp
	ret
.type Guard_Guard, @function
.size Guard_Guard, .-Guard_Guard
.text
.globl Guard_sum
Guard_sum:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	movl 4(%rdi), %ecx
	addl %ecx, %eax
	addq $16, %rsp
	ret
.type Guard_sum, @function
.size Guard_sum, .-Guard_sum
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $48, %rsp
	movl $10, -48(%rbp)
	movl $2, %edx
	movl $1, %esi
	leaq -48(%rbp), %rdi
	callq Vec_setii@plt
	movl -48(%rbp), %eax
	cmpl $1, %eax
	jnz .Lbb14
	leaq -48(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -12(%rbp)
	cmpl $2, %eax
	jnz .Lbb13
	leaq -32(%rbp), %rdi
	callq Guard_Guard@plt
	leaq -32(%rbp), %rdi
	callq Guard_sum@plt
	movl %eax, -16(%rbp)
	cmpl $3, %eax
	jnz .Lbb12
	movl $0, %eax
	jmp .Lbb15
.Lbb12:
	movl $3, %eax
	jmp .Lbb15
.Lbb13:
	movl $2, %eax
	jmp .Lbb15
.Lbb14:
	movl $1, %eax
.Lbb15:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
