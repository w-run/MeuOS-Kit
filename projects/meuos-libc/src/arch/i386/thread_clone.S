.text

/* long __meuos_thread_clone(start, arg, stack_top, control, tls)
 *
 * cdecl ABI; after 4 pushes (ebx,esi,edi,ebp) the stack layout is:
 *   20(%esp)=start  24(%esp)=arg  28(%esp)=stack_top
 *   32(%esp)=control  36(%esp)=tls
 *
 * i386 clone(120): ebx=flags, ecx=child_stack, edx=ptid, esi=ctid, edi=newtls
 *
 * We do NOT use CLONE_SETTLS (the kernel expects a struct user_desc* on
 * i386, not a raw pointer); the child calls __meuos_set_tls() instead.
 *
 * Flags = CLONE_VM|FS|FILES|SIGHAND|THREAD|SYSVSEM|PARENT_SETTID|
 *         CHILD_CLEARTID|CHILD_SETTID = 0x01350F00 */
.globl __meuos_thread_clone
.type __meuos_thread_clone,@function
__meuos_thread_clone:
	pushl %ebx
	pushl %esi
	pushl %edi
	pushl %ebp

	/* Save control into %edx for later ptid/ctid; it is at 32(%esp). */
	movl 32(%esp), %edx   /* control */
	/* Load stack_top into %ecx and build the child stack frame. */
	movl 28(%esp), %ecx   /* stack_top */
	subl $16, %ecx
	movl 20(%esp), %ebp   /* start */
	movl 24(%esp), %edi   /* arg */
	movl 36(%esp), %eax   /* tls */
	movl %ebp, 0(%ecx)    /* child[0] = start */
	movl %edi, 4(%ecx)    /* child[1] = arg */
	movl %edx, 8(%ecx)    /* child[2] = control */
	movl %eax, 12(%ecx)   /* child[3] = tls */

	/* clone(flags=ebx, child_stack=ecx, ptid=edx, ctid=esi, newtls=edi) */
	movl $0x01350F00, %ebx /* flags (without CLONE_SETTLS) */
	/* ecx already = child_stack */
	/* edx already = control = &control->tid (offset 0) */
	movl %edx, %esi        /* ctid = &control->tid */
	xorl %edi, %edi        /* newtls = 0 */
	movl $120, %eax        /* SYS_clone (i386) */
	int $0x80
	testl %eax, %eax
	jnz 1f                 /* parent: eax = child tid */

	/* ---- child: %esp = child_stack = stack_top-16 ---- */
	popl %ebp              /* start */
	popl %edi              /* arg */
	popl %esi              /* control */
	popl %ebx              /* tls */
	testl %ebx, %ebx
	jz 2f
	pushl %ebx
	call __meuos_set_tls
	addl $4, %esp
	testl %eax, %eax
	js 3f
2:
	pushl %edi             /* arg */
	call *%ebp             /* start(arg) */
	addl $4, %esp
	pushl %esi             /* control (2nd arg) */
	pushl %eax             /* result (1st arg) */
	call __meuos_thread_finish
	ud2
3:
	movl $1, %eax          /* SYS_exit */
	movl $127, %ebx        /* exit code 127 */
	int $0x80
	ud2
1:
	/* ---- parent ---- */
	popl %ebp
	popl %edi
	popl %esi
	popl %ebx
	ret

.section .note.GNU-stack,"",@progbits
