.text

/* long __syscall6(number, a1, a2, a3, a4, a5, a6)
 * Translate AAPCS64 to the Linux aarch64 syscall ABI:
 *   syscall number in x8, arguments in x0-x5, enter via svc #0.
 *
 * Entry (AAPCS64): x0=number, x1=a1, x2=a2, x3=a3, x4=a4, x5=a5, x6=a6
 * Target (Linux):  x8=number, x0=a1, x1=a2, x2=a3, x3=a4, x4=a5, x5=a6
 * aarch64 has 8 argument registers so all seven C arguments arrive in x0-x6. */
.globl __meuos_syscall6
.type __meuos_syscall6,@function
__meuos_syscall6:
	mov x8, x0
	mov x0, x1
	mov x1, x2
	mov x2, x3
	mov x3, x4
	mov x4, x5
	mov x5, x6
	svc #0
	ret

.section .note.GNU-stack,"",@progbits
