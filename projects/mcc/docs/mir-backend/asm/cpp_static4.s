.text
.globl Registry_Registry
Registry_Registry:
	endbr64
	movq Registry_count@gotpcrel(%rip), %rax
	movl (%rax), %eax
	addl $1, %eax
	movq Registry_count@gotpcrel(%rip), %rcx
	movl %eax, (%rcx)
	ret
.type Registry_Registry, @function
.size Registry_Registry, .-Registry_Registry
.text
.globl Registry_getS
Registry_getS:
	endbr64
	movq Registry_count@gotpcrel(%rip), %rax
	movl (%rax), %eax
	ret
.type Registry_getS, @function
.size Registry_getS, .-Registry_getS
.data
.balign 4
.globl Registry_count
Registry_count:
	.int 0
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $64, %rsp
	leaq -64(%rbp), %rdi
	callq Registry_Registry@plt
	leaq -48(%rbp), %rdi
	callq Registry_Registry@plt
	leaq -32(%rbp), %rdi
	callq Registry_Registry@plt
	callq Registry_getS@plt
	movl %eax, -16(%rbp)
	cmpl $3, %eax
	jnz .Lbb6
	movl $0, %eax
	jmp .Lbb7
.Lbb6:
	movl $1, %eax
.Lbb7:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
