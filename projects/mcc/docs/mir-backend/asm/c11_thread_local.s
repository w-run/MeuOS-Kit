.section .tbss,"awT"
.balign 4
local:
	.fill 4,1,0
.text
worker:
	endbr64
	pushq %rbp
	movq %rsp, %rbp
	subq $24, %rsp
	pushq %rbx
	movq %rdi, -8(%rbp)
	movl (%rdi), %eax
	movl %eax, %fs:local@tpoff
	movl $5, %edx
	movl $1, %esi
	movq %rdi, %rbx
	leaq ready(%rip), %rdi
	callq __atomic_fetch_add_4@plt
	movq %rbx, %rdi
.p2align 4
.Lbb1:
	movl $5, %esi
	movq %rdi, %rbx
	leaq ready(%rip), %rdi
	callq __atomic_load_4@plt
	movq %rbx, %rdi
	movl %eax, -16(%rbp)
	cmpl $2, %eax
	jnz .Lbb1
	movl $0, %eax
.p2align 4
.Lbb3:
	cmpl $1000, %eax
	jge .Lbb5
	movl %fs:local@tpoff, %ecx
	movl %ecx, %fs:local@tpoff
	addl $1, %eax
	jmp .Lbb3
.Lbb5:
	movl %fs:local@tpoff, %eax
	movl %eax, (%rdi)
	movl $0, %eax
	popq %rbx
	leave
	ret
.type worker, @function
.size worker, .-worker
.data
.balign 1
.Lstring.3:
	.ascii "FAIL\000"
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
	subq $64, %rsp
	movl $3, -32(%rbp)
	movl $7, -16(%rbp)
	leaq -32(%rbp), %rcx
	leaq worker(%rip), %rdx
	movl $0, %esi
	leaq -64(%rbp), %rdi
	callq pthread_create@plt
	leaq -16(%rbp), %rcx
	leaq worker(%rip), %rdx
	movl $0, %esi
	leaq -48(%rbp), %rdi
	callq pthread_create@plt
	movq -64(%rbp), %rdi
	movl $0, %esi
	callq pthread_join@plt
	movq -48(%rbp), %rdi
	movl $0, %esi
	callq pthread_join@plt
	movl -32(%rbp), %eax
	cmpl $3, %eax
	jnz .Lbb11
	movl -16(%rbp), %eax
	cmpl $7, %eax
	jnz .Lbb11
	movl %fs:local@tpoff, %eax
	cmpl $0, %eax
	jnz .Lbb11
	leaq .Lstring.4(%rip), %rdi
	callq puts@plt
	movl $0, %eax
	jmp .Lbb12
.Lbb11:
	leaq .Lstring.3(%rip), %rdi
	callq puts@plt
	movl $1, %eax
.Lbb12:
	leave
	ret
.type main, @function
.size main, .-main
.bss
.balign 4
ready:
	.fill 4,1,0
.section .note.GNU-stack,"",@progbits
