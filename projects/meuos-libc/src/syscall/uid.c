/* syscall/uid.c — POSIX.1-2008 process identity setting (setuid/setgid/
 * seteuid/setegid).
 *
 * setuid(uid)/setgid(gid) set the permission-related real/effective/saved
 * semantics handled by the kernel.  seteuid/isetegid have no standalone
 * syscall on any architecture, so they are expressed as setreuid(-1, e) /
 * setregid(-1, g) — the -1 keeps real id unchanged while assigning the
 * effective id, which is exactly seteuid(3)/setegid(3) semantics.  This
 * keeps a single path across all architectures.  Zero GNU dependency. */

#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include "../internal/syscall.h"

/* x86_64 native syscall numbers (internal stable ids); mapped per-arch in
 * internal/syscall.h. */
#ifndef LINUX_SYS_SETUID
#define LINUX_SYS_SETUID 105
#endif
#ifndef LINUX_SYS_SETGID
#define LINUX_SYS_SETGID 106
#endif
#ifndef LINUX_SYS_SETREUID
#define LINUX_SYS_SETREUID 113
#endif
#ifndef LINUX_SYS_SETREGID
#define LINUX_SYS_SETREGID 114
#endif

int
setuid(uid_t uid)
{
	long r = __syscall1(LINUX_SYS_SETUID, uid);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}

int
setgid(gid_t gid)
{
	long r = __syscall1(LINUX_SYS_SETGID, gid);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}

int
seteuid(uid_t euid)
{
	long r = __syscall2(LINUX_SYS_SETREUID, -1, euid);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}

int
setegid(gid_t egid)
{
	long r = __syscall2(LINUX_SYS_SETREGID, -1, egid);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}
