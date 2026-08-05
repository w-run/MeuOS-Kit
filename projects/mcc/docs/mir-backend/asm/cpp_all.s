.text
.globl Vec_zero
Vec_zero:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $0, 8(%rdi)
	movl $0, 4(%rdi)
	movl $0, (%rdi)
	addq $16, %rsp
	ret
.type Vec_zero, @function
.size Vec_zero, .-Vec_zero
.text
.globl Vec_setiii
Vec_setiii:
	endbr64
	subq $32, %rsp
	movl %ecx, 16(%rsp)
	movl %edx, 12(%rsp)
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl %esi, (%rdi)
	movl %edx, 4(%rdi)
	movl %ecx, 8(%rdi)
	addq $32, %rsp
	ret
.type Vec_setiii, @function
.size Vec_setiii, .-Vec_setiii
.text
.globl Vec_sum
Vec_sum:
	endbr64
	subq $32, %rsp
	movq %rdi, 0(%rsp)
	movl $0, %eax
	movl $0, %ecx
.p2align 4
.Lbb6:
	cmpl $3, %ecx
	jge .Lbb13
	cmpl $0, %ecx
	jnz .Lbb9
	movl (%rdi), %edx
	addl %edx, %eax
	jmp .Lbb12
.Lbb9:
	cmpl $1, %ecx
	jz .Lbb11
	movl 8(%rdi), %edx
	addl %edx, %eax
	jmp .Lbb12
.Lbb11:
	movl 4(%rdi), %edx
	addl %edx, %eax
.Lbb12:
	addl $1, %ecx
	jmp .Lbb6
.Lbb13:
	addq $32, %rsp
	ret
.type Vec_sum, @function
.size Vec_sum, .-Vec_sum
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $32, %rsp
	leaq -32(%rbp), %rdi
	callq Vec_zero@plt
	movl $30, %ecx
	movl $20, %edx
	movl $10, %esi
	leaq -32(%rbp), %rdi
	callq Vec_setiii@plt
	leaq -32(%rbp), %rdi
	callq Vec_sum@plt
	movl %eax, -12(%rbp)
	cmpl $60, %eax
	jnz .Lbb18
	movl $3, %ecx
	movl $2, %edx
	movl $1, %esi
	leaq -32(%rbp), %rdi
	callq Vec_setiii@plt
	leaq -32(%rbp), %rdi
	callq Vec_sum@plt
	movl %eax, -16(%rbp)
	cmpl $6, %eax
	jnz .Lbb17
	movl $0, %eax
	jmp .Lbb19
.Lbb17:
	movl $2, %eax
	jmp .Lbb19
.Lbb18:
	movl $1, %eax
.Lbb19:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
