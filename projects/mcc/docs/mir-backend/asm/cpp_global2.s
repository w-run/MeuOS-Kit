.text
.globl Counter_Counter
Counter_Counter:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $5, (%rdi)
	addq $16, %rsp
	ret
.type Counter_Counter, @function
.size Counter_Counter, .-Counter_Counter
.text
.globl Counter_inc
Counter_inc:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addl $1, %eax
	movl %eax, (%rdi)
	addq $16, %rsp
	ret
.type Counter_inc, @function
.size Counter_inc, .-Counter_inc
.text
.globl Counter_get
Counter_get:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addq $16, %rsp
	ret
.type Counter_get, @function
.size Counter_get, .-Counter_get
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $32, %rsp
	leaq -32(%rbp), %rdi
	callq Counter_Counter@plt
	movq g1@gotpcrel(%rip), %rdi
	callq Counter_inc@plt
	movq g2@gotpcrel(%rip), %rdi
	callq Counter_inc@plt
	movq g2@gotpcrel(%rip), %rdi
	callq Counter_inc@plt
	movq g1@gotpcrel(%rip), %rdi
	callq Counter_get@plt
	movl %eax, -8(%rbp)
	cmpl $6, %eax
	jnz .Lbb12
	movq g2@gotpcrel(%rip), %rdi
	callq Counter_get@plt
	movl %eax, -12(%rbp)
	cmpl $7, %eax
	jnz .Lbb11
	leaq -32(%rbp), %rdi
	callq Counter_get@plt
	movl %eax, -16(%rbp)
	cmpl $5, %eax
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
.bss
.balign 4
.globl g1
g1:
	.fill 4,1,0
.bss
.balign 4
.globl g2
g2:
	.fill 4,1,0
.text
.globl __mxx_global_var_init
__mxx_global_var_init:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $176, %rsp
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
	movq g1@gotpcrel(%rip), %rdi
	callq Counter_Counter@plt
	movq g2@gotpcrel(%rip), %rdi
	callq Counter_Counter@plt
	leave
	ret
.type __mxx_global_var_init, @function
.size __mxx_global_var_init, .-__mxx_global_var_init
.section .init_array,"aw"
.balign 8
.quad __mxx_global_var_init
.section .note.GNU-stack,"",@progbits
