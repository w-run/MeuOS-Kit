.data
.balign 1
.Lstring.2:
	.ascii "FAIL: sizeof long long\000"
.data
.balign 1
.Lstring.3:
	.ascii "FAIL: sizeof unsigned long long\000"
.data
.balign 1
.Lstring.4:
	.ascii "FAIL: LLONG_MAX\000"
.data
.balign 1
.Lstring.5:
	.ascii "FAIL: ULLONG_MAX\000"
.data
.balign 1
.Lstring.6:
	.ascii "FAIL: long long add\000"
.data
.balign 1
.Lstring.7:
	.ascii "FAIL: long long sub\000"
.data
.balign 1
.Lstring.8:
	.ascii "FAIL: long long mul\000"
.data
.balign 1
.Lstring.9:
	.ascii "FAIL: ull wrap\000"
.data
.balign 1
.Lstring.10:
	.ascii "FAIL: long long cmp\000"
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
