/* syscall/getrusage.c — POSIX.1-2008 process resource usage (getrusage).
 *
 * getrusage(who, usage) fills *usage with resource-usage statistics for the
 * current process (RUSAGE_SELF), its waited-for children (RUSAGE_CHILDREN),
 * or the calling thread (RUSAGE_THREAD).  The public struct rusage begins
 * with two struct timevals whose layout (tv_sec + tv_usec, padded to the
 * long) matches the kernel's on the 64-bit-time_t architectures this libc
 * targets, so the struct passes straight through.  Zero GNU dependency. */

#include <sys/resource.h>
#include <errno.h>
#include "../internal/syscall.h"

/* x86_64 native syscall number (internal stable id); mapped per-arch in
 * internal/syscall.h. */
#ifndef LINUX_SYS_GETRUSAGE
#define LINUX_SYS_GETRUSAGE 98
#endif

int
getrusage(int who, struct rusage *usage)
{
	long r = __syscall2(LINUX_SYS_GETRUSAGE, who, (long)usage);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}
