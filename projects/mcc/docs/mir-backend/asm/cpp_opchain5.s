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
	pushq %rbp
	movq %rsp, %rbp
	subq $48, %rsp
	movq %rsi, -8(%rbp)
	movq %rdi, -16(%rbp)
	movq %rsi, -40(%rbp)
	movl (%rdi), %eax
	movl -40(%rbp), %ecx
	movl %eax, %esi
	addl %ecx, %esi
	leaq -32(%rbp), %rdi
	callq Vec_Veci@plt
	movq -32(%rbp), %rax
	leave
	ret
.type Vec_operator_ploVec, @function
.size Vec_operator_ploVec, .-Vec_operator_ploVec
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $176, %rsp
	movl $1, %esi
	leaq -144(%rbp), %rdi
	callq Vec_Veci@plt
	movl $2, %esi
	leaq -128(%rbp), %rdi
	callq Vec_Veci@plt
	movl $3, %esi
	leaq -112(%rbp), %rdi
	callq Vec_Veci@plt
	movl $4, %esi
	leaq -96(%rbp), %rdi
	callq Vec_Veci@plt
	movq -128(%rbp), %rsi
	leaq -144(%rbp), %rdi
	callq Vec_operator_ploVec@plt
	movq %rax, -8(%rbp)
	movq %rax, -152(%rbp)
	movq -112(%rbp), %rsi
	leaq -152(%rbp), %rdi
	callq Vec_operator_ploVec@plt
	movq %rax, -16(%rbp)
	movq %rax, -160(%rbp)
	movq -96(%rbp), %rsi
	leaq -160(%rbp), %rdi
	callq Vec_operator_ploVec@plt
	movq %rax, -24(%rbp)
	movq %rax, -168(%rbp)
	movl -168(%rbp), %eax
	movl %eax, -80(%rbp)
	leaq -80(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -28(%rbp)
	cmpl $10, %eax
	jnz .Lbb16
	leaq -144(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -32(%rbp)
	cmpl $1, %eax
	jnz .Lbb15
	leaq -128(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -36(%rbp)
	cmpl $2, %eax
	jnz .Lbb14
	leaq -112(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -40(%rbp)
	cmpl $3, %eax
	jnz .Lbb13
	leaq -96(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -44(%rbp)
	cmpl $4, %eax
	jnz .Lbb12
	movl $0, %eax
	jmp .Lbb17
.Lbb12:
	leaq -96(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -48(%rbp)
	addl $50, %eax
	jmp .Lbb17
.Lbb13:
	leaq -112(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -52(%rbp)
	addl $40, %eax
	jmp .Lbb17
.Lbb14:
	leaq -128(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -56(%rbp)
	addl $30, %eax
	jmp .Lbb17
.Lbb15:
	leaq -144(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -60(%rbp)
	addl $20, %eax
	jmp .Lbb17
.Lbb16:
	leaq -80(%rbp), %rdi
	callq Vec_get@plt
	movl %eax, -64(%rbp)
	addl $10, %eax
.Lbb17:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
