.text
ieq:
	endbr64
	subq $16, %rsp
	movsd %xmm1, 8(%rsp)
	movsd %xmm0, 0(%rsp)
	cvttsd2sil %xmm0, %ecx
	cvttsd2sil %xmm1, %eax
	cmpl %eax, %ecx
	setz %al
	movzbl %al, %eax
	addq $16, %rsp
	ret
.type ieq, @function
.size ieq, .-ieq
.data
.balign 1
.Lstring.3:
	.ascii "FAIL: (float)(char)\000"
.data
.balign 1
.Lstring.4:
	.ascii "FAIL: (float)(int)\000"
.data
.balign 1
.Lstring.5:
	.ascii "FAIL: (float)(long)\000"
.data
.balign 1
.Lstring.6:
	.ascii "FAIL: (float)(unsigned)\000"
.data
.balign 1
.Lstring.7:
	.ascii "FAIL: (double)(char)\000"
.data
.balign 1
.Lstring.8:
	.ascii "FAIL: (double)(int)\000"
.data
.balign 1
.Lstring.9:
	.ascii "FAIL: (double)(long)\000"
.data
.balign 1
.Lstring.10:
	.ascii "FAIL: (int)(float)\000"
.data
.balign 1
.Lstring.11:
	.ascii "FAIL: (int)(double)\000"
.data
.balign 1
.Lstring.12:
	.ascii "FAIL: (long)(double)\000"
.data
.balign 1
.Lstring.13:
	.ascii "FAIL: 2.0==2\000"
.data
.balign 1
.Lstring.14:
	.ascii "FAIL: 5.1<5\000"
.data
.balign 1
.Lstring.15:
	.ascii "FAIL: 4.9<5\000"
.data
.balign 1
.Lstring.16:
	.ascii "FAIL: 5.1<=5\000"
.data
.balign 1
.Lstring.17:
	.ascii "FAIL: 4.9<=5\000"
.data
.balign 1
.Lstring.18:
	.ascii "FAIL: 2.3+3.8\000"
.data
.balign 1
.Lstring.19:
	.ascii "FAIL: 2.3-3.8\000"
.data
.balign 1
.Lstring.20:
	.ascii "FAIL: -3.8\000"
.data
.balign 1
.Lstring.21:
	.ascii "FAIL: 3.3*4\000"
.data
.balign 1
.Lstring.22:
	.ascii "FAIL: 5.0/2\000"
.data
.balign 1
.Lstring.23:
	.ascii "FAIL: !3.\000"
.data
.balign 1
.Lstring.24:
	.ascii "FAIL: !0.\000"
.data
.balign 1
.Lstring.25:
	.ascii "FAIL: 0.0?3:5\000"
.data
.balign 1
.Lstring.26:
	.ascii "FAIL: 1.2?3:5\000"
.data
.balign 1
.Lstring.27:
	.ascii "PASS\000"
.text
.globl main
main:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $96, %rsp
	movsd ".Lfp0"(%rip), %xmm1
	movsd ".Lfp0"(%rip), %xmm0
	callq ieq
	movl %eax, -8(%rbp)
	movl $5, %ecx
	cvtsi2sd %ecx, %xmm1
	movsd %xmm1, -72(%rbp)
	movl $3, %ecx
	cvtsi2sd %ecx, %xmm1
	movsd %xmm1, -88(%rbp)
	cmpl $0, %eax
	jnz .Lbb4
	leaq .Lstring.3(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb41
.Lbb4:
	movsd ".Lfp0"(%rip), %xmm1
	movsd ".Lfp0"(%rip), %xmm0
	callq ieq
	movl %eax, -12(%rbp)
	cmpl $0, %eax
	jnz .Lbb6
	leaq .Lstring.4(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb41
.Lbb6:
	movsd ".Lfp0"(%rip), %xmm1
	movsd ".Lfp0"(%rip), %xmm0
	callq ieq
	movl %eax, -16(%rbp)
	cmpl $0, %eax
	jnz .Lbb8
	leaq .Lstring.5(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb41
.Lbb8:
	movsd ".Lfp0"(%rip), %xmm1
	movsd ".Lfp0"(%rip), %xmm0
	callq ieq
	movl %eax, -20(%rbp)
	cmpl $0, %eax
	jnz .Lbb10
	leaq .Lstring.6(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb41
.Lbb10:
	movsd ".Lfp0"(%rip), %xmm1
	movsd ".Lfp0"(%rip), %xmm0
	callq ieq
	movl %eax, -24(%rbp)
	cmpl $0, %eax
	jnz .Lbb12
	leaq .Lstring.7(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb41
.Lbb12:
	movsd ".Lfp0"(%rip), %xmm1
	movsd ".Lfp0"(%rip), %xmm0
	callq ieq
	movl %eax, -28(%rbp)
	cmpl $0, %eax
	jnz .Lbb14
	leaq .Lstring.8(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb41
.Lbb14:
	movsd ".Lfp0"(%rip), %xmm1
	movsd ".Lfp0"(%rip), %xmm0
	callq ieq
	movl %eax, -32(%rbp)
	cmpl $0, %eax
	jnz .Lbb16
	leaq .Lstring.9(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb41
.Lbb16:
	movsd ".Lfp0"(%rip), %xmm1
	movsd ".Lfp0"(%rip), %xmm0
	callq ieq
	movl %eax, -36(%rbp)
	cmpl $0, %eax
	jnz .Lbb18
	leaq .Lstring.10(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb41
.Lbb18:
	movsd ".Lfp0"(%rip), %xmm1
	movsd ".Lfp0"(%rip), %xmm0
	callq ieq
	movl %eax, -40(%rbp)
	cmpl $0, %eax
	jnz .Lbb20
	leaq .Lstring.11(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb41
.Lbb20:
	movsd ".Lfp0"(%rip), %xmm1
	movsd ".Lfp0"(%rip), %xmm0
	callq ieq
	movl %eax, -44(%rbp)
	cmpl $0, %eax
	jnz .Lbb22
	leaq .Lstring.12(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb41
.Lbb22:
	movsd ".Lfp2"(%rip), %xmm1
	movsd ".Lfp1"(%rip), %xmm0
	callq ieq
	movl %eax, -48(%rbp)
	cmpl $0, %eax
	jnz .Lbb24
	leaq .Lstring.18(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb41
.Lbb24:
	movsd ".Lfp4"(%rip), %xmm1
	movsd ".Lfp3"(%rip), %xmm0
	callq ieq
	movl %eax, -52(%rbp)
	cmpl $0, %eax
	jnz .Lbb26
	leaq .Lstring.19(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb41
.Lbb26:
	movsd ".Lfp6"(%rip), %xmm1
	movsd ".Lfp5"(%rip), %xmm0
	callq ieq
	movl %eax, -56(%rbp)
	cmpl $0, %eax
	jnz .Lbb28
	leaq .Lstring.20(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb41
.Lbb28:
	movsd ".Lfp8"(%rip), %xmm1
	movsd ".Lfp7"(%rip), %xmm0
	callq ieq
	movl %eax, -60(%rbp)
	cmpl $0, %eax
	jnz .Lbb30
	leaq .Lstring.21(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb41
.Lbb30:
	movsd ".Lfp10"(%rip), %xmm1
	movsd ".Lfp9"(%rip), %xmm0
	callq ieq
	movl %eax, -64(%rbp)
	cmpl $0, %eax
	jnz .Lbb32
	leaq .Lstring.22(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb41
.Lbb32:
	movsd ".Lfp11"(%rip), %xmm1
	movsd ".Lfp11"(%rip), %xmm0
	callq ieq
	movl %eax, -76(%rbp)
	cmpl $0, %eax
	jnz .Lbb34
	leaq .Lstring.23(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb41
.Lbb34:
	movsd ".Lfp12"(%rip), %xmm1
	movsd ".Lfp12"(%rip), %xmm0
	callq ieq
	movsd -72(%rbp), %xmm1
	movl %eax, -80(%rbp)
	cmpl $0, %eax
	jnz .Lbb36
	leaq .Lstring.24(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb41
.Lbb36:
	movsd ".Lfp13"(%rip), %xmm0
	callq ieq
	movsd -88(%rbp), %xmm1
	movl %eax, -92(%rbp)
	cmpl $0, %eax
	jnz .Lbb38
	leaq .Lstring.25(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb41
.Lbb38:
	movsd ".Lfp14"(%rip), %xmm0
	callq ieq
	movl %eax, -96(%rbp)
	cmpl $0, %eax
	jnz .Lbb40
	leaq .Lstring.26(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb41
.Lbb40:
	leaq .Lstring.27(%rip), %rdi
	callq puts@plt
	movl $0, %eax
.Lbb41:
	leave
	ret
.type main, @function
.size main, .-main
/* floating point constants */
.section .rodata
.p2align 3
.Lfp0:
	.quad 4630122629401935872 /* 35.000000 */

.section .rodata
.p2align 3
.Lfp1:
	.quad 4618441417868443648 /* 6.000000 */

.section .rodata
.p2align 3
.Lfp2:
	.quad 4618554007859127910 /* 6.100000 */

.section .rodata
.p2align 3
.Lfp3:
	.quad -4616189618054758400 /* -1.000000 */

.section .rodata
.p2align 3
.Lfp4:
	.quad -4613937818241073152 /* -1.500000 */

.section .rodata
.p2align 3
.Lfp5:
	.quad -4609434218613702656 /* -3.000000 */

.section .rodata
.p2align 3
.Lfp6:
	.quad -4607632778762754458 /* -3.800000 */

.section .rodata
.p2align 3
.Lfp7:
	.quad 4623507967449235456 /* 13.000000 */

.section .rodata
.p2align 3
.Lfp8:
	.quad 4623620557439919718 /* 13.200000 */

.section .rodata
.p2align 3
.Lfp9:
	.quad 4611686018427387904 /* 2.000000 */

.section .rodata
.p2align 3
.Lfp10:
	.quad 4612811918334230528 /* 2.500000 */

.section .rodata
.p2align 3
.Lfp11:
	.quad 0 /* 0.000000 */

.section .rodata
.p2align 3
.Lfp12:
	.quad 4607182418800017408 /* 1.000000 */

.section .rodata
.p2align 3
.Lfp13:
	.quad 4617315517961601024 /* 5.000000 */

.section .rodata
.p2align 3
.Lfp14:
	.quad 4613937818241073152 /* 3.000000 */

.section .note.GNU-stack,"",@progbits
