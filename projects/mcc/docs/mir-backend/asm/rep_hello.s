.data
.balign 1
.Lstring.2:
	.ascii "hello from mcc\012\000"
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	leaq .Lstring.2(%rip), %rdi
	callq printf@plt
	movl $0, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
