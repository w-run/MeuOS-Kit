.text

/* riscv64 signal restore stub.
 *
 * The kernel enters a signal handler with ra pointing at this stub; when the
 * handler returns, control lands here and we invoke rt_sigreturn (139) to
 * restore the interrupted context.  Both symbols share one implementation,
 * matching musl's riscv64 restore.s. */

.globl __restore
.hidden __restore
.type __restore,@function
__restore:
.globl __restore_rt
.hidden __restore_rt
.type __restore_rt,@function
__restore_rt:
	li a7, 139		/* SYS_rt_sigreturn (riscv64/asm-generic) */
	ecall

.section .note.GNU-stack,"",@progbits
