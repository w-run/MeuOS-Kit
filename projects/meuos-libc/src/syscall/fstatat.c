/* syscall/fstatat.c — newfstatat (x86_64: 262) */
#include <errno.h>
#include <sys/stat.h>
#include "../internal/syscall.h"

#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif
#define LINUX_SYS_FSTATAT 262

int fstatat(int dirfd, const char *path, struct stat *buf, int flags)
{
	long r = __syscall4(LINUX_SYS_FSTATAT, dirfd, (long)path, (long)buf, flags);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}