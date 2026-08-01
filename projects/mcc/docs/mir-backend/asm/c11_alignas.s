.data
.balign 1
.Lstring.2:
	.ascii "FAIL\000"
.data
.balign 1
.Lstring.3:
	.ascii "PASS\000"
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	leaq aligned(%rip), %rax
	andq $63, %rax
	cmpq $0, %rax
	jnz .Lbb2
	leaq .Lstring.3(%rip), %rdi
	callq puts@plt
	movl $0, %eax
	jmp .Lbb3
.Lbb2:
	leaq .Lstring.2(%rip), %rdi
	callq puts@plt
	movl $1, %eax
.Lbb3:
	leave
	ret
.type main, @function
.size main, .-main
.bss
.balign 64
aligned:
	.fill 1,1,0
.section .note.GNU-stack,"",@progbits
