/* process/group.c — POSIX.1-2008 process groups and sessions
 * (setpgid/getpgid/getpgrp/setsid/getsid).
 *
 * setpgid(pid, pgid) puts pid into process group pgid (000 = its own pid);
 * getpgid(pid) and getpgrp() report a process's group id; setsid() starts a
 * new session and makes the caller its leader; getsid(pid) reports the
 * session of pid.  getpgrp() is defined by POSIX to equal getpgid(0), so it
 * is expressed that way -- the asm-generic kernels have no standalone pgrp
 * syscall, and this keeps every architecture on one path.  Zero GNU
 * dependency. */

#include <unistd.h>
#include <errno.h>
#include "../internal/syscall.h"

/* x86_64 native syscall numbers (internal stable ids); mapped per-arch in
 * internal/syscall.h. */
#ifndef LINUX_SYS_SETPGID
#define LINUX_SYS_SETPGID 109
#endif
#ifndef LINUX_SYS_GETPGID
#define LINUX_SYS_GETPGID 121
#endif
#ifndef LINUX_SYS_SETSID
#define LINUX_SYS_SETSID 112
#endif
#ifndef LINUX_SYS_GETSID
#define LINUX_SYS_GETSID 124
#endif

int
setpgid(pid_t pid, pid_t pgid)
{
	long r = __syscall2(LINUX_SYS_SETPGID, pid, pgid);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}

pid_t
getpgid(pid_t pid)
{
	long r = __syscall1(LINUX_SYS_GETPGID, pid);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return (pid_t)r;
}

pid_t
getpgrp(void)
{
	/* POSIX: getpgrp() == getpgid(0). */
	return getpgid(0);
}

pid_t
setsid(void)
{
	long r = __syscall0(LINUX_SYS_SETSID);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return (pid_t)r;
}

pid_t
getsid(pid_t pid)
{
	long r = __syscall1(LINUX_SYS_GETSID, pid);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return (pid_t)r;
}
