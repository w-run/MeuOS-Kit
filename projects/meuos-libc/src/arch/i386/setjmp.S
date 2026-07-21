.text

/* i386 jmp_buf layout (six 32-bit words):
 *   [0] ebx  [1] esi  [2] edi  [3] ebp  [4] caller esp  [5] return eip
 */
.globl setjmp
.type setjmp,@function
setjmp:
.globl _setjmp
.type _setjmp,@function
_setjmp:
	movl 4(%esp), %edx
	movl %ebx, 0(%edx)
	movl %esi, 4(%edx)
	movl %edi, 8(%edx)
	movl %ebp, 12(%edx)
	leal 4(%esp), %eax
	movl %eax, 16(%edx)
	movl (%esp), %eax
	movl %eax, 20(%edx)
	xorl %eax, %eax
	ret

.globl longjmp
.type longjmp,@function
longjmp:
.globl _longjmp
.type _longjmp,@function
_longjmp:
	movl 4(%esp), %edx
	movl 8(%esp), %eax
	testl %eax, %eax
	jnz 1f
	movl $1, %eax
1:
	movl 0(%edx), %ebx
	movl 4(%edx), %esi
	movl 8(%edx), %edi
	movl 12(%edx), %ebp
	movl 16(%edx), %esp
	jmp *20(%edx)

/* sigjmp_buf layout on i386:
 *   [0..5] jmp_buf, [7] mask-saved flag, [8..9] sigset_t (64 bits).
 * The unused word [6] keeps the mask portion naturally aligned. */
.globl sigsetjmp
.type sigsetjmp,@function
sigsetjmp:
	movl 4(%esp), %edx
	movl %ebx, 0(%edx)
	movl %esi, 4(%edx)
	movl %edi, 8(%edx)
	movl %ebp, 12(%edx)
	leal 4(%esp), %eax
	movl %eax, 16(%edx)
	movl (%esp), %eax
	movl %eax, 20(%edx)
	cmpl $0, 8(%esp)
	je 2f
	/* rt_sigprocmask(SIG_BLOCK, NULL, &env[8], 8), i386 syscall 175. */
	xorl %ebx, %ebx
	xorl %ecx, %ecx
	leal 32(%edx), %edi
	movl %edi, %edx
	movl $8, %esi
	movl $175, %eax
	int $0x80
	movl 4(%esp), %edx
	movl $1, 28(%edx)
	xorl %eax, %eax
	ret
2:
	movl $0, 28(%edx)
	xorl %eax, %eax
	ret

.globl siglongjmp
.type siglongjmp,@function
siglongjmp:
	/* Keep env in EDI and the result in ESI.  The syscall uses EBX/ECX/EDX
	 * and EDI only as an argument-size register, so reload env afterwards. */
	movl 4(%esp), %edi
	movl 8(%esp), %esi
	testl %esi, %esi
	jnz 3f
	movl $1, %esi
3:
	cmpl $0, 28(%edi)
	je 4f
	pushl %edi
	movl $2, %ebx
	leal 32(%edi), %ecx
	xorl %edx, %edx
	movl $8, %edi
	movl $175, %eax
	int $0x80
	popl %edi
4:
	movl %esi, %eax
	movl 0(%edi), %ebx
	movl 4(%edi), %esi
	movl 8(%edi), %ecx
	movl 12(%edi), %ebp
	movl 20(%edi), %edx
	movl 16(%edi), %esp
	movl %ecx, %edi
	jmp *%edx

.section .note.GNU-stack,"",@progbits
