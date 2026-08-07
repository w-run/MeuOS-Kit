/* syscall/fchownat.c — fchownat (x86_64: 260) */
#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif
#define LINUX_SYS_FCHOWNAT 260

int fchownat(int dirfd, const char *path, uid_t owner, gid_t group, int flags)
{
	long r = __syscall5(LINUX_SYS_FCHOWNAT, dirfd, (long)path, owner, group, flags);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}