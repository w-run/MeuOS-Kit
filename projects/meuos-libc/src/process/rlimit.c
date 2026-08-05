/* process/rlimit.c — POSIX.1-2008 getrlimit/setrlimit (resource limits).
 *
 * getrlimit(resource, rlim) reads the current soft/hard limits; setrlimit
 * updates them.  Both are built on the prlimit64 syscall: getrlimit
 * queries the current process (pid=0, new=NULL); setrlimit updates and
 * optionally returns the old limit (new!=NULL, old!=NULL if requested).
 * Zero GNU dependency; getrlimit/setrlimit are POSIX.1-2008 in core libc. */

#include <sys/resource.h>
#include <errno.h>
#include "../internal/syscall.h"

#ifndef LINUX_SYS_PRLIMIT64
#define LINUX_SYS_PRLIMIT64 302      /* native x86_64; passed through on
                                     * archs that lack a translation entry
                                     * (the prlimit64 entry is set up for
                                     * x86_64 / i386 in internal/syscall.h). */
#endif

int
getrlimit(int resource, struct rlimit *rlim)
{
	long v;
	if (!rlim) { errno = EINVAL; return -1; }
	v = __syscall4(LINUX_SYS_PRLIMIT64, 0, resource, 0, (long)rlim);
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return 0;
}

int
setrlimit(int resource, const struct rlimit *rlim)
{
	long v;
	if (!rlim) { errno = EINVAL; return -1; }
	v = __syscall4(LINUX_SYS_PRLIMIT64, 0, resource, (long)rlim, 0);
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return 0;
}
