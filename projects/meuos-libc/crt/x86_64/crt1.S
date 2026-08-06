.text

/* Linux x86_64 process entry.  The kernel supplies argc at %rsp followed by
 * argv[] and envp[].  All the startup work -- capturing argv[0]/auxv, running
 * .preinit_array/.init_array constructors, registering the .fini_array walk
 * with atexit(), calling main() and passing its status to exit() -- lives in
 * the portable __libc_start_main() (src/startup.c), so this stub only has to
 * marshal arguments and align the stack.
 *
 * __libc_start_main(main, argc, argv, init, fini, rtld_fini, stack_end)
 * mirrors the GNU signature so a program built by gcc/clang against this
 * libc links against the symbol it expects.  Our own crt passes NULL for
 * init/fini/rtld_fini: the array walk inside covers them. */
.globl _start
.type _start,@function
_start:
	xorq %rbp, %rbp
	movq (%rsp), %rdi		/* argc */
	leaq 8(%rsp), %rsi		/* argv */
	leaq 8(%rsi,%rdi,8), %rdx	/* envp */
	movq %rdx, environ(%rip)
	movq %rsp, %r12			/* stash kernel sp (callee-saved) */
	andq $-16, %rsp
	movq %rdx, %rdi			/* envp */
	call __meuos_tls_init
	/* __libc_start_main(main, argc, argv, init, fini, rtld_fini, stack_end)
	 *
	 * init = __libc_csu_init: walks .preinit_array, calls _init(),
	 * then walks .init_array so compat archives have a named entry
	 * point to hook into startup.
	 * fini = __libc_csu_fini: walks .fini_array in reverse and calls
	 * _fini(), registered with atexit so it runs on the exit() path.
	 * rtld_fini = NULL: we are statically linked, no dynamic loader. */
	movq $main, %rdi
	movq (%r12), %rsi		/* argc */
	leaq 8(%r12), %rdx		/* argv */
	leaq __libc_csu_init(%rip), %rcx /* init */
	leaq __libc_csu_fini(%rip), %r8  /* fini */
	xorl %r9d, %r9d			/* rtld_fini = NULL */
	/* The 7th argument goes on the stack.  SysV x86_64 wants %rsp 16-byte
	 * aligned at the call instruction, i.e. 8 mod 16 once the return
	 * address is pushed; two pushes keep the alignment and leave the
	 * argument in the right slot. */
	pushq %r12			/* padding */
	pushq %r12			/* stack_end (7th arg) */
	call __libc_start_main
	hlt				/* unreachable: it exit()s */

.section .note.GNU-stack,"",@progbits

.bss
.align 8
.globl environ
.type environ,@object
.size environ,8
environ:
	.quad 0
