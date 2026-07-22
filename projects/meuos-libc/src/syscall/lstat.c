#include <errno.h>
#include <sys/stat.h>
#include "../internal/syscall.h"

#if defined(__i386__)
#include "../internal/arch/i386/statx.h"
int
lstat(const char *path, struct stat *result)
{
	struct meuos_statx sx;
	long value = __syscall6(LINUX_SYS_STATX_I386, MEUOS_AT_FDCWD,
		(long)path, MEUOS_AT_SYMLINK_NOFOLLOW,
		MEUOS_STATX_BASIC_STATS, (long)&sx, 0);
	if (__syscall_error(value)) {
		errno = (int)-value;
		return -1;
	}
	return meuos_statx_to_stat(&sx, result);
}
#elif defined(__aarch64__)
/* aarch64 没有 lstat，用 statx + AT_SYMLINK_NOFOLLOW 复刻 lstat() 语义。 */
#include "../internal/arch/i386/statx.h"
int
lstat(const char *path, struct stat *result)
{
	struct meuos_statx sx;
	long value = __syscall6(LINUX_SYS_STATX, MEUOS_AT_FDCWD,
		(long)path, MEUOS_AT_SYMLINK_NOFOLLOW,
		MEUOS_STATX_BASIC_STATS, (long)&sx, 0);
	if (__syscall_error(value)) {
		errno = (int)-value;
		return -1;
	}
	return meuos_statx_to_stat(&sx, result);
}
#else
#define LINUX_SYS_LSTAT 6
int lstat(const char *path, struct stat *result) { long value = __syscall2(LINUX_SYS_LSTAT, (long)path, (long)result); if (__syscall_error(value)) { errno = (int)-value; return -1; } return 0; }
#endif
