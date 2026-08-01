.data
.balign 1
.Lstring.2:
	.ascii "FAIL: sizeof int64_t\000"
.data
.balign 1
.Lstring.3:
	.ascii "FAIL: sizeof uint64_t\000"
.data
.balign 1
.Lstring.4:
	.ascii "FAIL: sizeof int32_t\000"
.data
.balign 1
.Lstring.5:
	.ascii "FAIL: sizeof uint32_t\000"
.data
.balign 1
.Lstring.6:
	.ascii "FAIL: sizeof int16_t\000"
.data
.balign 1
.Lstring.7:
	.ascii "FAIL: sizeof uint16_t\000"
.data
.balign 1
.Lstring.8:
	.ascii "FAIL: sizeof int8_t\000"
.data
.balign 1
.Lstring.9:
	.ascii "FAIL: sizeof uint8_t\000"
.data
.balign 1
.Lstring.10:
	.ascii "FAIL: int64_t val\000"
.data
.balign 1
.Lstring.11:
	.ascii "FAIL: uint64_t val\000"
.data
.balign 1
.Lstring.12:
	.ascii "FAIL: INT32_MAX\000"
.data
.balign 1
.Lstring.13:
	.ascii "FAIL: INT32_MIN\000"
.data
.balign 1
.Lstring.14:
	.ascii "FAIL: UINT32_MAX\000"
.data
.balign 1
.Lstring.15:
	.ascii "FAIL: INT64_MIN\000"
.data
.balign 1
.Lstring.16:
	.ascii "PASS\000"
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	leaq .Lstring.16(%rip), %rdi
	callq puts@plt
	movl $0, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
