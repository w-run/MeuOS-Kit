.data
.balign 1
.Lstring.2:
	.ascii "FAIL: sizeof flex\000"
.data
.balign 1
.Lstring.3:
	.ascii "FAIL: offsetof flex.len\000"
.data
.balign 1
.Lstring.4:
	.ascii "FAIL: offsetof flex.data\000"
.data
.balign 1
.Lstring.5:
	.ascii "FAIL: flex pointer\000"
.data
.balign 1
.Lstring.6:
	.ascii "PASS\000"
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	leaq .Lstring.6(%rip), %rdi
	callq puts@plt
	movl $0, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
