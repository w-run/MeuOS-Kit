/* aarch64 set_tls.S - install TPIDR_EL0 (thread pointer).
 *
 * aarch64 has no arch_prctl-equivalent syscall for programming the TLS
 * register; the kernel sets TPIDR_EL0 directly via CLONE_SETTLS during
 * clone(2).  The main thread must install it itself with `msr tpidr_el0`.
 * mcc does not support inline asm, so this stub exposes a C-callable
 * __meuos_set_tls(tp) used by tls.c. */

.text
.globl __meuos_set_tls
.type __meuos_set_tls,@function
__meuos_set_tls:
	msr tpidr_el0, x0
	ret
.section .note.GNU-stack,"",@progbits
