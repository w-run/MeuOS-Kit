#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#if defined(__aarch64__)
/* aarch64 没有 rename(82)，改用 renameat(olddirfd, oldpath, newdirfd, newpath)。
 * 两端都用 AT_FDCWD 复刻 rename() 语义。 */
#define MEUOS_AT_FDCWD (-100)
#define LINUX_SYS_RENAMEAT 264

int
rename(const char *old_path, const char *new_path)
{
	long result = __syscall4(LINUX_SYS_RENAMEAT, MEUOS_AT_FDCWD,
		(long)old_path, MEUOS_AT_FDCWD, (long)new_path);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}
#else
#define LINUX_SYS_RENAME 82

int
rename(const char *old_path, const char *new_path)
{
	long result = __syscall2(LINUX_SYS_RENAME, (long)old_path, (long)new_path);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}
#endif
