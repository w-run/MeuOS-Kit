.text

/* LoongArch64 rt_sigreturn restorer.  loongarch64 only provides rt_sigreturn
 * (139); there is no legacy sigreturn, so both aliases issue the same syscall.
 * The kernel installs its own signal-return trampoline, so no userspace
 * restorer is required (see src/signal/signal.c, which omits SA_RESTORER for
 * this architecture). */

.globl __restore
.type __restore,@function
__restore:
.globl __restore_rt
.type __restore_rt,@function
__restore_rt:
	li.w $a7, 139
	syscall 0

.section .note.GNU-stack,"",@progbits
