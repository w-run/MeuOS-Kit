/* syscall/unlinkat.c — unlinkat (x86_64: 263) */
#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_UNLINKAT 263

int unlinkat(int dirfd, const char *path, int flags)
{
	long r = __syscall3(LINUX_SYS_UNLINKAT, dirfd, (long)path, flags);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}