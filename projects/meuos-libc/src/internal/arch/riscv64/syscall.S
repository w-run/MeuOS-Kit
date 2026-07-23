.text

/* long __meuos_syscall6(long number, long a1, long a2, long a3,
 *                        long a4, long a5, long a6)
 *
 * Translate the RISC-V LP64D C calling convention into the Linux riscv64
 * syscall ABI:
 *   - syscall number in a7
 *   - arguments in a0-a5
 *   - enter via ecall; return value in a0 (negative => errno)
 *
 * Entry (LP64D): a0 = number, a1..a6 = args (a6 lives in a6, the 7th
 * integer register).  The Linux kernel reads only a0-a5, so the 7th C
 * argument (a6) maps onto the 6th syscall slot (a5). */
.globl __meuos_syscall6
.type __meuos_syscall6,@function
__meuos_syscall6:
	mv a7, a0
	mv a0, a1
	mv a1, a2
	mv a2, a3
	mv a3, a4
	mv a4, a5
	mv a5, a6
	ecall
	ret

.section .note.GNU-stack,"",@progbits
