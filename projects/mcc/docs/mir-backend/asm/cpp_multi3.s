.text
.globl X_X
X_X:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $1, (%rdi)
	addq $16, %rsp
	ret
.type X_X, @function
.size X_X, .-X_X
.text
.globl Y_Y
Y_Y:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $2, (%rdi)
	addq $16, %rsp
	ret
.type Y_Y, @function
.size Y_Y, .-Y_Y
.text
.globl Z_Z
Z_Z:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $3, (%rdi)
	addq $16, %rsp
	ret
.type Z_Z, @function
.size Z_Z, .-Z_Z
.text
.globl D_total
D_total:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	movl 4(%rdi), %ecx
	addl %ecx, %eax
	movl 8(%rdi), %ecx
	addl %ecx, %eax
	addq $16, %rsp
	ret
.type D_total, @function
.size D_total, .-D_total
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $32, %rsp
	leaq -32(%rbp), %rdi
	callq X_X@plt
	leaq -32(%rbp), %rax
	movq %rax, %rdi
	addq $4, %rdi
	callq Y_Y@plt
	leaq -32(%rbp), %rax
	movq %rax, %rdi
	addq $8, %rdi
	callq Z_Z@plt
	leaq -32(%rbp), %rdi
	callq D_total@plt
	movl %eax, -16(%rbp)
	cmpl $6, %eax
	jnz .Lbb10
	movl $0, %eax
.Lbb10:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
