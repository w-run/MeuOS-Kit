/* x86_64 dlstart.S — entry point for the MeuOS dynamic linker.
 *
 * The kernel loads ld.so as the PT_INTERP interpreter, sets up
 * argc/argv/envp/auxv on the stack, and jumps to _dlstart.  We save
 * the stack pointer, call rtld_main(), then jump to the main binary's
 * entry point that rtld_main returns. */
.section .text
.globl _dlstart
.type _dlstart,@function
_dlstart:
	/* rbx = original stack pointer (argc, argv, envp, auxv) */
	movq	%rsp, %rbx
	/* Align to 16 bytes for the C ABI */
	andq	$-16, %rsp
	/* First argument = original stack pointer */
	movq	%rbx, %rdi
	call	rtld_main
	/* rax = entry point of the main binary */
	/* Restore the original stack so _start sees the right layout */
	movq	%rbx, %rsp
	xorq	%rbp, %rbp
	jmp	*%rax
