.text
assert_eq:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	movl %edi, %eax
	movq %rdx, %rdi
	movq %rdi, -8(%rbp)
	movl %esi, -12(%rbp)
	movl %eax, -16(%rbp)
	cmpl %esi, %eax
	jz .Lbb2
	callq puts@plt
	movl $1, %edi
	callq exit@plt
.Lbb2:
	leave
	ret
.type assert_eq, @function
.size assert_eq, .-assert_eq
.data
.balign 1
.Lstring.3:
	.ascii "FAIL 1: simple ({})\000"
.data
.balign 1
.Lstring.4:
	.ascii "FAIL 2: decl+value\000"
.data
.balign 1
.Lstring.5:
	.ascii "FAIL 3a: side effect\000"
.data
.balign 1
.Lstring.6:
	.ascii "FAIL 3b: result\000"
.data
.balign 1
.Lstring.7:
	.ascii "FAIL 4: for loop\000"
.data
.balign 1
.Lstring.8:
	.ascii "FAIL 5: nested\000"
.data
.balign 1
.Lstring.9:
	.ascii "PASS 6: void\000"
.data
.balign 1
.Lstring.10:
	.ascii "PASS\000"
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $16, %rsp
	leaq .Lstring.3(%rip), %rdx
	movl $42, %esi
	movl $42, %edi
	callq assert_eq
	leaq .Lstring.4(%rip), %rdx
	movl $15, %esi
	movl $15, %edi
	callq assert_eq
	leaq .Lstring.5(%rip), %rdx
	movl $100, %esi
	movl $100, %edi
	callq assert_eq
	leaq .Lstring.6(%rip), %rdx
	movl $101, %esi
	movl $101, %edi
	callq assert_eq
	movl $0, %eax
	movl $0, %ecx
.p2align 4
.Lbb5:
	cmpl $10, %ecx
	jge .Lbb7
	addl %ecx, %eax
	addl $1, %ecx
	jmp .Lbb5
.Lbb7:
	movl %eax, %esi
	leaq .Lstring.7(%rip), %rdx
	movl $45, %edi
	callq assert_eq
	leaq .Lstring.8(%rip), %rdx
	movl $14, %esi
	movl $14, %edi
	callq assert_eq
	leaq .Lstring.9(%rip), %rdi
	callq puts@plt
	leaq .Lstring.10(%rip), %rdi
	callq puts@plt
	movl $0, %eax
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
