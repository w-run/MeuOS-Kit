/* syscall/fchmodat.c — fchmodat (x86_64: 268) */
#include <errno.h>
#include <sys/stat.h>
#include "../internal/syscall.h"

#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif
#define LINUX_SYS_FCHMODAT 268

int fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
	long r = __syscall4(LINUX_SYS_FCHMODAT, dirfd, (long)path, mode, flags);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}