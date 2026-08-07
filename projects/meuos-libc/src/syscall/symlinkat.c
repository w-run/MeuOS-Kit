/* syscall/symlinkat.c — symlinkat (x86_64: 266) */
#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_SYMLINKAT 266

int symlinkat(const char *target, int newdirfd, const char *linkpath)
{
	long r = __syscall3(LINUX_SYS_SYMLINKAT, (long)target, newdirfd, (long)linkpath);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}