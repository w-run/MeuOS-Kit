.text

/* void __meuos_restore_rt(void)
 * Signal-return trampoline.  The kernel jumps here when a signal handler
 * returns; issue rt_sigreturn to unwind the signal frame.  aarch64 does
 * not use SA_RESTORER (the kernel installs its own trampoline), but the
 * symbol is still referenced by src/signal/signal.c on targets that do. */
.globl __meuos_restore_rt
.type __meuos_restore_rt,@function
__meuos_restore_rt:
	mov x8, #139		/* SYS_rt_sigreturn (aarch64) */
	svc #0
	brk #0			/* unreachable: rt_sigreturn does not return */

.section .note.GNU-stack,"",@progbits
