.text

/* long __meuos_syscall6(long number, long a1, long a2, long a3,
 *                        long a4, long a5, long a6)
 *
 * Translate the LoongArch64 LP64D C calling convention into the Linux
 * loongarch64 syscall ABI:
 *   - syscall number in $a7
 *   - arguments in $a0-$a5
 *   - enter via `syscall 0`; return value in $a0 (negative => errno)
 *
 * Entry (LP64D): $a0 = number, $a1..$a6 = args ($a6 lives in $a6, the 7th
 * integer register).  The Linux kernel reads only $a0-$a5, so the 7th C
 * argument ($a6) maps onto the 6th syscall slot ($a5). */

.globl __meuos_syscall6
.type __meuos_syscall6,@function
__meuos_syscall6:
	move $a7, $a0
	move $a0, $a1
	move $a1, $a2
	move $a2, $a3
	move $a3, $a4
	move $a4, $a5
	move $a5, $a6
	syscall 0
	jr $ra

.section .note.GNU-stack,"",@progbits
