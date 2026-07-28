/* syscall.c — raw Linux syscall wrappers for ld.so (no libc). */
#include "rtld.h"

long
rtld_syscall(long n, long a1, long a2, long a3,
             long a4, long a5, long a6)
{
	register long rax __asm__("rax") = n;
	register long rdi __asm__("rdi") = a1;
	register long rsi __asm__("rsi") = a2;
	register long rdx __asm__("rdx") = a3;
	register long r10 __asm__("r10") = a4;
	register long r8  __asm__("r8") = a5;
	register long r9  __asm__("r9") = a6;
	__asm__ __volatile__("syscall"
	                     : "+r"(rax)
	                     : "r"(rdi), "r"(rsi), "r"(rdx),
	                       "r"(r10), "r"(r8), "r"(r9)
	                     : "rcx", "r11", "memory");
	return rax;
}

void *
rtld_mmap(void *addr, size_t length, int prot, int flags, int fd, long offset)
{
	return (void *)rtld_syscall(SYS_mmap, (long)addr, (long)length,
	                           prot, flags, fd, offset);
}

int
rtld_mprotect(void *addr, size_t length, int prot)
{
	return (int)rtld_syscall(SYS_mprotect, (long)addr, (long)length,
	                        prot, 0, 0, 0);
}

int
rtld_munmap(void *addr, size_t length)
{
	return (int)rtld_syscall(SYS_munmap, (long)addr, (long)length,
	                        0, 0, 0, 0);
}

int
rtld_open(const char *path, int flags)
{
	return (int)rtld_syscall(SYS_open, (long)path, flags, 0, 0, 0, 0);
}

int
rtld_read(int fd, void *buf, size_t count)
{
	return (int)rtld_syscall(SYS_read, fd, (long)buf, (long)count, 0, 0, 0);
}

int
rtld_write(int fd, const void *buf, size_t count)
{
	return (int)rtld_syscall(SYS_write, fd, (long)buf, (long)count, 0, 0, 0);
}

int
rtld_close(int fd)
{
	return (int)rtld_syscall(SYS_close, fd, 0, 0, 0, 0, 0);
}

long
rtld_lseek(int fd, long offset, int whence)
{
	return rtld_syscall(SYS_lseek, fd, offset, whence, 0, 0, 0);
}

void *
rtld_brk(void *addr)
{
	return (void *)rtld_syscall(SYS_brk, (long)addr, 0, 0, 0, 0, 0);
}

void
rtld_exit(int code)
{
	rtld_syscall(SYS_exit_group, code, 0, 0, 0, 0, 0);
	/* fallback if exit_group fails */
	rtld_syscall(SYS_exit, code, 0, 0, 0, 0, 0);
}

long
rtld_arch_prctl(int code, unsigned long addr)
{
	return rtld_syscall(SYS_arch_prctl, code, (long)addr, 0, 0, 0, 0);
}

/* Write an error message and exit.  Used before we can call any libc. */
static size_t
local_strlen(const char *s)
{
	size_t n = 0;
	while (s[n]) n++;
	return n;
}

void
rtld_die(const char *msg)
{
	const char *prefix = "ld.so: ";
	const char *nl = "\n";
	rtld_write(2, prefix, local_strlen(prefix));
	rtld_write(2, msg, local_strlen(msg));
	rtld_write(2, nl, 1);
	rtld_exit(127);
}
