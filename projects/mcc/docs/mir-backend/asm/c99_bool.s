.data
.balign 1
.Lstring.2:
	.ascii "FAIL: _Bool=1\000"
.data
.balign 1
.Lstring.3:
	.ascii "FAIL: _Bool=0\000"
.data
.balign 1
.Lstring.4:
	.ascii "FAIL: _Bool=42 -> 1\000"
.data
.balign 1
.Lstring.5:
	.ascii "FAIL: (_Bool)0\000"
.data
.balign 1
.Lstring.6:
	.ascii "FAIL: (_Bool)1\000"
.data
.balign 1
.Lstring.7:
	.ascii "FAIL: (_Bool)100\000"
.data
.balign 1
.Lstring.8:
	.ascii "FAIL: (_Bool)0.0\000"
.data
.balign 1
.Lstring.9:
	.ascii "FAIL: (_Bool)0.5\000"
.data
.balign 1
.Lstring.10:
	.ascii "FAIL: sizeof _Bool\000"
.data
.balign 1
.Lstring.11:
	.ascii "PASS\000"
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	leaq .Lstring.11(%rip), %rdi
	callq puts@plt
	movl $0, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
