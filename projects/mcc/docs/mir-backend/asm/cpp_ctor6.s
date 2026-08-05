.text
.globl Vec_Vec
Vec_Vec:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $0, (%rdi)
	movl $0, 4(%rdi)
	addq $16, %rsp
	ret
.type Vec_Vec, @function
.size Vec_Vec, .-Vec_Vec
.text
.globl Vec_Vecii
Vec_Vecii:
	endbr64
	subq $16, %rsp
	movl %edx, 12(%rsp)
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl %esi, (%rdi)
	movl %edx, 4(%rdi)
	addq $16, %rsp
	ret
.type Vec_Vecii, @function
.size Vec_Vecii, .-Vec_Vecii
.text
.globl Vec_sum
Vec_sum:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	movl 4(%rdi), %ecx
	addl %ecx, %eax
	addq $16, %rsp
	ret
.type Vec_sum, @function
.size Vec_sum, .-Vec_sum
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $64, %rsp
	leaq -64(%rbp), %rdi
	callq Vec_Vec@plt
	movl $6, %edx
	movl $5, %esi
	leaq -48(%rbp), %rdi
	callq Vec_Vecii@plt
	movl -44(%rbp), %edx
	movl $3, %esi
	leaq -32(%rbp), %rdi
	callq Vec_Vecii@plt
	leaq -64(%rbp), %rdi
	callq Vec_sum@plt
	movl %eax, -8(%rbp)
	cmpl $0, %eax
	jnz .Lbb12
	leaq -48(%rbp), %rdi
	callq Vec_sum@plt
	movl %eax, -12(%rbp)
	cmpl $11, %eax
	jnz .Lbb11
	leaq -32(%rbp), %rdi
	callq Vec_sum@plt
	movl %eax, -16(%rbp)
	cmpl $9, %eax
	jnz .Lbb10
	movl $0, %eax
	jmp .Lbb13
.Lbb10:
	movl $3, %eax
	jmp .Lbb13
.Lbb11:
	movl $2, %eax
	jmp .Lbb13
.Lbb12:
	movl $1, %eax
.Lbb13:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
