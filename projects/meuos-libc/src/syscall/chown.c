#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include "../internal/syscall.h"

#if defined(__aarch64__)
/* aarch64 没有 chown(92)，改用 fchownat(dirfd, path, owner, group, flag)。
 * 用 AT_FDCWD + 0 flags 复刻 chown() 语义。需要 5 个参数，用 __syscall6
 * 多传一个 0，第 6 个参数被 aarch64 syscall ABI 忽略。 */
#define MEUOS_AT_FDCWD (-100)
#define LINUX_SYS_FCHOWNAT 260
int
chown(const char *path, uid_t owner, gid_t group)
{
	long r = __syscall6(LINUX_SYS_FCHOWNAT, MEUOS_AT_FDCWD,
		(long)path, (long)owner, (long)group, 0, 0);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}
#else
#define LINUX_SYS_CHOWN 92
int chown(const char *path, uid_t owner, gid_t group) {
	long r = __syscall3(LINUX_SYS_CHOWN, (long)path, (long)owner, (long)group);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}
#endif
