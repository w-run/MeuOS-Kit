.text

/* Linux i386 process entry.  The kernel supplies argc at (%esp) followed
 * by argv[] and envp[].  Set up TLS (%gs) before handing control to the
 * C library so that _Thread_local variables (notably errno) work.
 *
 * All the startup work -- capturing argv[0]/auxv, running the
 * .preinit_array/.init_array constructors, registering the .fini_array
 * walk with atexit(), calling main() and passing its status to exit() --
 * lives in the portable __libc_start_main() (src/startup.c), so this stub
 * only marshals arguments.  The GNU signature is
 *   __libc_start_main(main, argc, argv, init, fini, rtld_fini, stack_end)
 * and cdecl passes all seven on the stack, rightmost first. */
.globl _start
.type _start,@function
_start:
	xorl %ebp, %ebp
	movl (%esp), %eax       /* argc */
	leal 4(%esp), %ebx      /* argv */
	leal 4(%ebx,%eax,4), %ecx  /* envp */
	movl %ecx, environ
	movl %esp, %esi         /* stash kernel sp (callee-saved) */
	/* Call __meuos_tls_init(envp) — cdecl, arg on stack. */
	pushl %ecx
	call __meuos_tls_init
	addl $4, %esp
	/* __libc_start_main(main, argc, argv, NULL, NULL, NULL, stack_end) */
	movl (%esi), %eax       /* argc */
	leal 4(%esi), %ebx      /* argv */
	pushl %esi              /* stack_end */
	pushl $0                /* rtld_fini */
	pushl $0                /* fini */
	pushl $0                /* init */
	pushl %ebx              /* argv */
	pushl %eax              /* argc */
	pushl $main
	call __libc_start_main
	hlt                     /* unreachable: it exit()s */

.bss
.align 4
.globl environ
.type environ,@object
.size environ,4
environ:
	.long 0

.section .note.GNU-stack,"",@progbits
