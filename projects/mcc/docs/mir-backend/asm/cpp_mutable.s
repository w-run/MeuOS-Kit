.text
.globl Cache_Cache
Cache_Cache:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl $0, (%rdi)
	movl $10, 4(%rdi)
	addq $16, %rsp
	ret
.type Cache_Cache, @function
.size Cache_Cache, .-Cache_Cache
.text
.globl Cache_getK
Cache_getK:
	endbr64
	subq $16, %rsp
	movq %rdi, 0(%rsp)
	movl (%rdi), %eax
	addl $1, %eax
	movl %eax, (%rdi)
	movl 4(%rdi), %eax
	addq $16, %rsp
	ret
.type Cache_getK, @function
.size Cache_getK, .-Cache_getK
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $32, %rsp
	leaq -32(%rbp), %rdi
	callq Cache_Cache@plt
	leaq -32(%rbp), %rdi
	callq Cache_getK@plt
	movl %eax, -12(%rbp)
	cmpl $10, %eax
	jnz .Lbb10
	leaq -32(%rbp), %rdi
	callq Cache_getK@plt
	movl %eax, -16(%rbp)
	cmpl $10, %eax
	jnz .Lbb9
	movl -32(%rbp), %eax
	cmpl $2, %eax
	jnz .Lbb8
	movl $0, %eax
	jmp .Lbb11
.Lbb8:
	movl $3, %eax
	jmp .Lbb11
.Lbb9:
	movl $2, %eax
	jmp .Lbb11
.Lbb10:
	movl $1, %eax
.Lbb11:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
