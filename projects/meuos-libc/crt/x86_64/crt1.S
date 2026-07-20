.text

/* Linux x86_64 process entry.  The kernel supplies argc at %rsp followed by
 * argv[] and envp[].  Preserve the C ABI stack alignment before calling main,
 * then terminate directly through SYS_exit. */
.globl _start
.type _start,@function
_start:
	xorq %rbp, %rbp
	movq (%rsp), %rdi
	leaq 8(%rsp), %rsi
	leaq 8(%rsi,%rdi,8), %rdx
	movq %rdx, environ(%rip)
	andq $-16, %rsp
	movq %rdx, %rdi
	call __meuos_tls_init
	movq (%rsp), %rdi
	leaq 8(%rsp), %rsi
	leaq 8(%rsi,%rdi,8), %rdx
	call main
	movl %eax, %edi
	movl $60, %eax
	syscall
	hlt

.section .note.GNU-stack,"",@progbits

.bss
.align 8
.globl environ
.type environ,@object
.size environ,8
environ:
	.quad 0
