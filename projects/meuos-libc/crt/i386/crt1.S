.text

/* Linux i386 process entry.  The kernel supplies argc at (%esp) followed
 * by argv[] and envp[].  Set up TLS (%gs) before calling main so that
 * _Thread_local variables (notably errno) work.  Then terminate via exit()
 * so that atexit handlers run and stdio buffers are flushed (important
 * when stdout is redirected to a file rather than a line-buffered tty). */
.globl _start
.type _start,@function
_start:
	xorl %ebp, %ebp
	movl (%esp), %eax       /* argc */
	leal 4(%esp), %ebx      /* argv */
	leal 4(%ebx,%eax,4), %ecx  /* envp */
	movl %ecx, environ
	/* Call __meuos_tls_init(envp) — cdecl, arg on stack. */
	pushl %ecx
	call __meuos_tls_init
	addl $4, %esp
	/* Reload argc/argv/envp for main. */
	movl (%esp), %eax       /* argc */
	leal 4(%esp), %ebx      /* argv */
	leal 4(%ebx,%eax,4), %ecx  /* envp */
	pushl %ecx              /* envp */
	pushl %ebx              /* argv */
	pushl %eax              /* argc */
	call main
	addl $12, %esp
	pushl %eax              /* exit status from main (cdecl arg) */
	call exit
	hlt

.bss
.align 4
.globl environ
.type environ,@object
.size environ,4
environ:
	.long 0

.section .note.GNU-stack,"",@progbits
