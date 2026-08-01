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
.globl X_setXi
X_setXi:
	endbr64
	subq $16, %rsp
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl %esi, (%rdi)
	addq $16, %rsp
	ret
.type X_setXi, @function
.size X_setXi, .-X_setXi
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
.globl Y_setYi
Y_setYi:
	endbr64
	subq $16, %rsp
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl %esi, (%rdi)
	addq $16, %rsp
	ret
.type Y_setYi, @function
.size Y_setYi, .-Y_setYi
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
.globl Z_setZi
Z_setZi:
	endbr64
	subq $16, %rsp
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl %esi, (%rdi)
	addq $16, %rsp
	ret
.type Z_setZi, @function
.size Z_setZi, .-Z_setZi
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
	subq $48, %rsp
	pushq %rbx
	pushq %r12
	leaq -48(%rbp), %rdi
	callq X_X@plt
	leaq -48(%rbp), %rax
	movq %rax, %rdi
	addq $4, %rdi
	movq %rdi, %rbx
	callq Y_Y@plt
	movq %rbx, %rdi
	leaq -48(%rbp), %rax
	movq %rax, %rbx
	addq $8, %rbx
	movq %rdi, %r12
	movq %rbx, %rdi
	callq Z_Z@plt
	movq %r12, %rdi
	movq %rdi, %r12
	leaq -48(%rbp), %rdi
	callq D_total@plt
	movq %r12, %rdi
	movl %eax, -28(%rbp)
	cmpl $6, %eax
	jnz .Lbb24
	movl $10, %esi
	movq %rdi, %r12
	leaq -48(%rbp), %rdi
	callq X_setXi@plt
	movq %r12, %rdi
	movl $20, %esi
	callq Y_setYi@plt
	movq %rbx, %rdi
	movl $30, %esi
	callq Z_setZi@plt
	movl -48(%rbp), %eax
	cmpl $10, %eax
	jnz .Lbb23
	movl -44(%rbp), %eax
	cmpl $20, %eax
	jnz .Lbb22
	movl -40(%rbp), %eax
	cmpl $30, %eax
	jnz .Lbb21
	leaq -48(%rbp), %rdi
	callq D_total@plt
	movl %eax, -32(%rbp)
	cmpl $60, %eax
	jnz .Lbb20
	movl $0, %eax
	jmp .Lbb25
.Lbb20:
	movl $5, %eax
	jmp .Lbb25
.Lbb21:
	movl $4, %eax
	jmp .Lbb25
.Lbb22:
	movl $3, %eax
	jmp .Lbb25
.Lbb23:
	movl $2, %eax
	jmp .Lbb25
.Lbb24:
	movl $1, %eax
.Lbb25:
	popq %r12
	popq %rbx
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
