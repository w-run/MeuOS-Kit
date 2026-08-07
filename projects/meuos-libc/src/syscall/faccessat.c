/* syscall/faccessat.c — faccessat (x86_64: 269) */
#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_FACCESSAT 269

int faccessat(int dirfd, const char *path, int mode, int flags)
{
	long r = __syscall4(LINUX_SYS_FACCESSAT, dirfd, (long)path, mode, flags);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}