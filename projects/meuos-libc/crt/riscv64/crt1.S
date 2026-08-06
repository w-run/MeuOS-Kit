.text

/* Linux riscv64 process entry. The kernel places argc at [sp], followed by
 * argv[], envp[], then auxv[]. RISC-V LP64D requires sp to be 16-byte aligned
 * at function-call boundaries. We set up TLS (tp) before calling main so that
 * _Thread_local variables (notably errno) work, then terminate via exit()
 * so that atexit handlers run and stdio buffers are flushed (important when
 * stdout is redirected to a file rather than a line-buffered tty).
 *
 * Unlike aarch64 (which needs `msr tpidr_el0` to program the thread pointer),
 * riscv64 has no arch_prctl-like syscall: the thread pointer lives in the tp
 * register (x4) and is written directly with `mv tp, a0`.
 * __meuos_tls_init returns the TLS base pointer, which we install here. The
 * kernel programs tp the same way for new threads via CLONE_SETTLS. */

.globl _start
.type _start,@function
_start:
	/* Initialize global pointer (gp/x3) to __global_pointer$ so that
	 * gp-relative accesses to .sdata/.sbss work.  This is required by
	 * the RISC-V ABI; the linker sets up __global_pointer$ to point to
	 * the middle of the small data area, and functions like exit() use
	 * gp-relative loads for global variables (handler_count, handlers).
	 * Without this, gp-relative accesses segfault on real hardware/VM
	 * (qemu-user tolerates a zero/uninitialized gp).
	 * NOTE: .option norelax is required — without it, the assembler
	 * relaxes "la gp, __global_pointer$" into a gp-relative access,
	 * creating a circular dependency. */
	.option push
	.option norelax
	la gp, __global_pointer$
	.option pop
	mv s0, sp			/* save kernel sp (argc/argv/envp/auxv) */
	andi sp, sp, -16		/* align sp for the calls below */

	/* argc = [s0]; argv = s0+8; envp = argv + (argc+1)*8 */
	ld a0, 0(s0)			/* argc */
	addi a1, s0, 8			/* argv */
	slli a2, a0, 3			/* argc * 8 */
	add a2, a2, a1			/* &argv[argc] */
	addi a2, a2, 8			/* envp */

	/* store envp into the global environ */
	lla a3, environ
	sd a2, 0(a3)

	/* tp = __meuos_tls_init(envp); it returns the TLS base pointer */
	mv a0, a2
	call __meuos_tls_init
	mv tp, a0

	/* __libc_start_main(main, argc, argv, init, fini, rtld_fini, stack_end).
	 *
	 * init = __libc_csu_init: walks .preinit_array, calls _init(),
	 * then walks .init_array so compat archives have a named entry
	 * point to hook into startup.
	 * fini = __libc_csu_fini: walks .fini_array in reverse and calls
	 * _fini(), registered with atexit so it runs on the exit() path.
	 * rtld_fini = NULL: we are statically linked, no dynamic loader.
	 *
	 * The portable startup wrapper (src/startup.c) captures argv[0]/auxv,
	 * runs the constructors, registers the .fini_array walk with atexit(),
	 * calls main() and hands its status to exit() -- which drains atexit
	 * and flushes stdio, so buffered printf output is not lost.
	 * All seven arguments fit in a0-a6. */
	lla a0, main			/* main */
	ld a1, 0(s0)			/* argc */
	addi a2, s0, 8			/* argv */
	lla a3, __libc_csu_init		/* init */
	lla a4, __libc_csu_fini		/* fini */
	li a5, 0			/* rtld_fini = NULL */
	mv a6, s0			/* stack_end */
	call __libc_start_main
	unimp				/* unreachable: it exit()s */

.bss
.align 3
.globl environ
.type environ,@object
.size environ,8
environ:
	.quad 0

.section .note.GNU-stack,"",@progbits
