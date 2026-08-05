/* stdio/sync.c — POSIX.1-2008 file synchronization (sync/fsync/fdatasync).
 *
 * sync() schedules all dirty buffers for writeback to disk; fsync(fd) flushes
 * a single fd's data and metadata and does not return until complete;
 * fdatasync(fd) flushes only the data (metadata whose need is not derived
 * from the data is skipped), which can be appreciably cheaper.  All are
 * thin wrapper over the Linux sync/fsync/fdatasync syscalls.  Zero GNU
 * dependency. */

#include <unistd.h>
#include <errno.h>
#include "../internal/syscall.h"

/* x86_64 native syscall numbers (internal stable ids); mapped per-arch in
 * internal/syscall.h. */
#ifndef LINUX_SYS_SYNC
#define LINUX_SYS_SYNC 162
#endif
#ifndef LINUX_SYS_FSYNC
#define LINUX_SYS_FSYNC 74
#endif
#ifndef LINUX_SYS_FDATASYNC
#define LINUX_SYS_FDATASYNC 75
#endif

void
sync(void)
{
	__syscall0(LINUX_SYS_SYNC);
}

int
fsync(int fd)
{
	long v = __syscall1(LINUX_SYS_FSYNC, fd);
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return 0;
}

int
fdatasync(int fd)
{
	long v = __syscall1(LINUX_SYS_FDATASYNC, fd);
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return 0;
}
