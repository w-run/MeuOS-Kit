/* syscall/mkdirat.c — mkdirat (x86_64: 258) */
#include <errno.h>
#include <sys/stat.h>
#include "../internal/syscall.h"

#define LINUX_SYS_MKDIRAT 258

int mkdirat(int dirfd, const char *path, mode_t mode)
{
	long r = __syscall3(LINUX_SYS_MKDIRAT, dirfd, (long)path, mode);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}