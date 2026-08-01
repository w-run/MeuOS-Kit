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
	subq $128, %rsp
	movl $0, -128(%rbp)
	movl $5, %edx
	movl $7, %esi
	leaq -128(%rbp), %rdi
	callq __atomic_store_4@plt
	movl $5, %esi
	leaq -128(%rbp), %rdi
	callq __atomic_load_4@plt
	movl %eax, -8(%rbp)
	cmpl $7, %eax
	jnz .Lbb30
	movl $5, %edx
	movl $0, %esi
	leaq -128(%rbp), %rdi
	callq __atomic_store_4@plt
	movl $5, %edx
	movl $1, %esi
	leaq -128(%rbp), %rdi
	callq __atomic_fetch_add_4@plt
	movl %eax, -12(%rbp)
	cmpl $0, %eax
	jnz .Lbb29
	movl $5, %esi
	leaq -128(%rbp), %rdi
	callq __atomic_load_4@plt
	movl %eax, -16(%rbp)
	cmpl $1, %eax
	jnz .Lbb29
	movl $5, %edx
	movl $1, %esi
	leaq -128(%rbp), %rdi
	callq __atomic_fetch_sub_4@plt
	movl %eax, -20(%rbp)
	cmpl $1, %eax
	jnz .Lbb28
	movl $5, %esi
	leaq -128(%rbp), %rdi
	callq __atomic_load_4@plt
	movl %eax, -24(%rbp)
	cmpl $0, %eax
	jnz .Lbb28
	movl $5, %edx
	movl $3, %esi
	leaq -128(%rbp), %rdi
	callq __atomic_fetch_add_4@plt
	movl %eax, -28(%rbp)
	addl $3, %eax
	cmpl $3, %eax
	jnz .Lbb27
	movl $5, %esi
	leaq -128(%rbp), %rdi
	callq __atomic_load_4@plt
	movl %eax, -32(%rbp)
	cmpl $3, %eax
	jnz .Lbb27
	movl $5, %edx
	movl $4, %esi
	leaq -128(%rbp), %rdi
	callq __atomic_fetch_or_4@plt
	movl %eax, -36(%rbp)
	cmpl $3, %eax
	jnz .Lbb26
	movl $5, %edx
	movl $6, %esi
	leaq -128(%rbp), %rdi
	callq __atomic_fetch_and_4@plt
	movl %eax, -40(%rbp)
	cmpl $7, %eax
	jnz .Lbb26
	movl $5, %edx
	movl $3, %esi
	leaq -128(%rbp), %rdi
	callq __atomic_fetch_xor_4@plt
	movl %eax, -44(%rbp)
	cmpl $6, %eax
	jnz .Lbb26
	movl $5, %esi
	leaq -128(%rbp), %rdi
	callq __atomic_load_4@plt
	movl %eax, -48(%rbp)
	cmpl $5, %eax
	jnz .Lbb26
	movl $5, %edx
	movl $9, %esi
	leaq -128(%rbp), %rdi
	callq __atomic_exchange_4@plt
	movl %eax, -52(%rbp)
	cmpl $5, %eax
	jnz .Lbb25
	movl $5, %esi
	leaq -128(%rbp), %rdi
	callq __atomic_load_4@plt
	movl %eax, -56(%rbp)
	cmpl $9, %eax
	jnz .Lbb25
	movl $9, -112(%rbp)
	movl $5, %r9d
	movl $5, %r8d
	movl $0, %ecx
	movl $11, %edx
	leaq -112(%rbp), %rsi
	leaq -128(%rbp), %rdi
	callq __atomic_compare_exchange_4@plt
	movl %eax, -60(%rbp)
	cmpl $0, %eax
	jz .Lbb24
	movl $5, %esi
	leaq -128(%rbp), %rdi
	callq __atomic_load_4@plt
	movl %eax, -64(%rbp)
	cmpl $11, %eax
	jnz .Lbb24
	movl $9, -112(%rbp)
	movl $5, %r9d
	movl $5, %r8d
	movl $0, %ecx
	movl $13, %edx
	leaq -112(%rbp), %rsi
	leaq -128(%rbp), %rdi
	callq __atomic_compare_exchange_4@plt
	movl %eax, -68(%rbp)
	cmpl $0, %eax
	jnz .Lbb23
	movl -112(%rbp), %eax
	cmpl $11, %eax
	jnz .Lbb23
	movl $0, -96(%rbp)
	movl $5, %edx
	movl $1, %esi
	leaq -96(%rbp), %rdi
	callq __atomic_exchange_4@plt
	movl %eax, -72(%rbp)
	cmpl $0, %eax
	jnz .Lbb22
	movl $5, %edx
	movl $1, %esi
	leaq -96(%rbp), %rdi
	callq __atomic_exchange_4@plt
	movl %eax, -76(%rbp)
	cmpl $0, %eax
	jz .Lbb22
	movl $5, %edx
	movl $0, %esi
	leaq -96(%rbp), %rdi
	callq __atomic_store_4@plt
	movl $5, %edx
	movl $1, %esi
	leaq -96(%rbp), %rdi
	callq __atomic_exchange_4@plt
	movl %eax, -80(%rbp)
	cmpl $0, %eax
	jnz .Lbb21
	leaq .Lstring.3(%rip), %rdi
	callq puts@plt
	movl $0, %eax
	jmp .Lbb31
.Lbb21:
	leaq .Lstring.2(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb31
.Lbb22:
	leaq .Lstring.2(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb31
.Lbb23:
	leaq .Lstring.2(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb31
.Lbb24:
	leaq .Lstring.2(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb31
.Lbb25:
	leaq .Lstring.2(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb31
.Lbb26:
	leaq .Lstring.2(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb31
.Lbb27:
	leaq .Lstring.2(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb31
.Lbb28:
	leaq .Lstring.2(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb31
.Lbb29:
	leaq .Lstring.2(%rip), %rdi
	callq puts@plt
	movl $1, %eax
	jmp .Lbb31
.Lbb30:
	leaq .Lstring.2(%rip), %rdi
	callq puts@plt
	movl $1, %eax
.Lbb31:
	leave
	ret
.type main, @function
.size main, .-main
.section .note.GNU-stack,"",@progbits
