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
.globl main
main:
	endbr64
	movq g@gotpcrel(%rip), %rax
	movl (%rax), %eax
	subl $5, %eax
	ret
.type main, @function
.size main, .-main
.bss
.balign 4
.globl g
g:
	.fill 4,1,0
.text
.globl __mxx_global_var_init
__mxx_global_var_init:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $18446744073709531904, %rsp
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
	movq g@gotpcrel(%rip), %rdi
	callq Counter_Counter@plt
	leave
	ret
.type __mxx_global_var_init, @function
.size __mxx_global_var_init, .-__mxx_global_var_init
.section .init_array,"aw"
.balign 8
.quad __mxx_global_var_init
.section .note.GNU-stack,"",@progbits
