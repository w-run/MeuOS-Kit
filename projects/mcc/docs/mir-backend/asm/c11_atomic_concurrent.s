.text
worker:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $24, %rsp
	pushq %rbx
	movl $0, %ebx
.p2align 4
.Lbb2:
	cmpl $1000, %ebx
	jge .Lbb4
	movl $5, %edx
	movl $1, %esi
	leaq counter(%rip), %rdi
	callq __atomic_fetch_add_4@plt
	addl $1, %ebx
	jmp .Lbb2
.Lbb4:
	movl $0, %eax
	popq %rbx
	leave
	ret
.type worker, @function
.size worker, .-worker
.data
.balign 1
.Lstring.3:
	.ascii "FAIL: counter = %d\134n\000"
.data
.balign 1
.Lstring.4:
	.ascii "PASS\000"
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $48, %rsp
	movl $0, %ecx
	leaq worker(%rip), %rdx
	movl $0, %esi
	leaq -48(%rbp), %rdi
	callq pthread_create@plt
	movl $0, %ecx
	leaq worker(%rip), %rdx
	movl $0, %esi
	leaq -32(%rbp), %rdi
	callq pthread_create@plt
	movq -48(%rbp), %rdi
	movl $0, %esi
	callq pthread_join@plt
	movq -32(%rbp), %rdi
	movl $0, %esi
	callq pthread_join@plt
	movl $5, %esi
	leaq counter(%rip), %rdi
	callq __atomic_load_4@plt
	movl %eax, -12(%rbp)
	cmpl $2000, %eax
	jnz .Lbb8
	leaq .Lstring.4(%rip), %rdi
	callq puts@plt
	movl $0, %eax
	jmp .Lbb9
.Lbb8:
	movl $5, %esi
	leaq counter(%rip), %rdi
	callq __atomic_load_4@plt
	movl %eax, %esi
	movl %esi, -16(%rbp)
	leaq .Lstring.3(%rip), %rdi
	callq printf@plt
	movl $1, %eax
.Lbb9:
	leave
	ret
.type main, @function
.size main, .-main
.bss
.balign 4
counter:
	.fill 4,1,0
.section .note.GNU-stack,"",@progbits
