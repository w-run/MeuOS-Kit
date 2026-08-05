/* process/prioritize.c — POSIX.1-2008 process scheduling priority
 * (getpriority/setpriority/nice).
 *
 * getpriority(which, who) returns the scheduling priority of a process,
 * process group, or user (-20 highest .. 19 lowest); setpriority sets it.
 * A returned -1 can be a legitimate priority, so getpriority distinguishes
 * failure by leaving errno set (checked against a pre-cleared errno).
 * nice(inc) is expressed via setpriority: the x86_64/aarch64 kernels expose
 * no standalone nice(2), only setpriority.  Ranges/privileges are policed by
 * the kernel.  Zero GNU dependency. */

#include <sys/resource.h>
#include <errno.h>
#include "../internal/syscall.h"

/* x86_64 native syscall numbers (internal stable ids); mapped per-arch in
 * internal/syscall.h. */
#ifndef LINUX_SYS_GETPRIORITY
#define LINUX_SYS_GETPRIORITY 140
#endif
#ifndef LINUX_SYS_SETPRIORITY
#define LINUX_SYS_SETPRIORITY 141
#endif

int
getpriority(int which, id_t who)
{
	errno = 0;
	long v = __syscall2(LINUX_SYS_GETPRIORITY, which, who);
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	/* kernel returns task_nice(p) + 20 (0..39); POSIX wants nice (-20..19). */
	return (int)v - 20;
}

int
setpriority(int which, id_t who, int prio)
{
	long v = __syscall3(LINUX_SYS_SETPRIORITY, which, who, prio);
	if (__syscall_error(v)) { errno = (int)-v; return -1; }
	return 0;
}

int
nice(int inc)
{
	int prio = getpriority(PRIO_PROCESS, 0);
	if (prio < 0 && errno)
		return -1;
	/* POSIX says nice(inc) adds inc to the current nice value; the kernel
	 * clamps to [-20, 19] and enforces privilege for raising priority. */
	if (setpriority(PRIO_PROCESS, 0, prio + inc) != 0)
		return -1;
	return prio + inc;
}
