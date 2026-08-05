.text
.globl Vec_Veci
Vec_Veci:
	endbr64
	subq $16, %rsp
	movl %esi, 8(%rsp)
	movq %rdi, 0(%rsp)
	movl %esi, (%rdi)
	addq $16, %rsp
	ret
.type Vec_Veci, @function
.size Vec_Veci, .-Vec_Veci
.text
.globl Vec_get
Vec_get:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addq $16, %rsp
	ret
.type Vec_get, @function
.size Vec_get, .-Vec_get
.text
.globl Vec_operator_ploVec
Vec_operator_ploVec:
	endbr64
	subq $32, %rsp
	movq %rsi, 16(%rsp)
	movq %rdi, 8(%rsp)
	movq %rsi, 0(%rsp)
	movl (%rdi), %eax
	movl 0(%rsp), %ecx
	addl %ecx, %eax
	movl %eax, (%rdi)
	movq (%rdi), %rax
	addq $32, %rsp
	ret
.type Vec_operator_ploVec, @function
.size Vec_operator_ploVec, .-Vec_operator_ploVec
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $192, %rsp
	movl $1, %esi
	leaq -160(%rbp), %rdi
	callq Vec_Veci@plt
	movl $2, %esi
	leaq -144(%rbp), %rdi
	callq Vec_Veci@plt
	movl $3, %esi
	leaq -128(%rbp), %rdi
	callq Vec_Veci@plt
	movl $4, %esi
	leaq -112(%rbp), %rdi
	callq Vec_Veci@plt
	movq -144(%rbp), %rsi
	leaq -160(%rbp), %rdi
	callq Vec_operator_ploVec@plt
	movq %rax, -8(%rbp)
	movq %rax, -168(%rbp)
	movl -168(%rbp), %eax
	movl %eax, -96(%rbp)
	leaq -96(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -20(%rbp)
	cmpl $3, %eax
	jnz .Lbb12
	movq -128(%rbp), %rsi
	leaq -96(%rbp), %rdi
	callq Vec_operator_ploVec@plt
	movq %rax, -16(%rbp)
	movq %rax, -184(%rbp)
	movl -184(%rbp), %eax
	movl %eax, -80(%rbp)
	leaq -80(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -24(%rbp)
	cmpl $6, %eax
	jnz .Lbb11
	movq -112(%rbp), %rsi
	leaq -80(%rbp), %rdi
	callq Vec_operator_ploVec@plt
	movq %rax, -32(%rbp)
	movq %rax, -176(%rbp)
	movl -176(%rbp), %eax
	movl %eax, -64(%rbp)
	leaq -64(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -36(%rbp)
	cmpl $10, %eax
	jnz .Lbb10
	movl $0, %eax
	jmp .Lbb13
.Lbb10:
	leaq -64(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -40(%rbp)
	addl $30, %eax
	jmp .Lbb13
.Lbb11:
	leaq -80(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -44(%rbp)
	addl $20, %eax
	jmp .Lbb13
.Lbb12:
	leaq -96(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -48(%rbp)
	addl $10, %eax
.Lbb13:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
