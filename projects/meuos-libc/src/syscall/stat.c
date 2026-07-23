#include <errno.h>
#include <sys/stat.h>
#include "../internal/syscall.h"

#if defined(__i386__)
#include "../internal/arch/i386/statx.h"
int
stat(const char *path, struct stat *result)
{
	struct meuos_statx sx;
	long value = __syscall6(LINUX_SYS_STATX_I386, MEUOS_AT_FDCWD,
		(long)path, 0, MEUOS_STATX_BASIC_STATS, (long)&sx, 0);
	if (__syscall_error(value)) {
		errno = (int)-value;
		return -1;
	}
	return meuos_statx_to_stat(&sx, result);
}
#elif defined(__aarch64__) || defined(__riscv) || defined(__loongarch64)
/* aarch64 kernel stat 布局与 struct stat 不同，改用 statx 取跨架构一致的
 * 布局后转换。statx 内部号用 x86_64 的 332，由翻译表转 aarch64 291。 */
#include "../internal/arch/i386/statx.h"
int
stat(const char *path, struct stat *result)
{
	struct meuos_statx sx;
	long value = __syscall6(LINUX_SYS_STATX, MEUOS_AT_FDCWD,
		(long)path, 0, MEUOS_STATX_BASIC_STATS, (long)&sx, 0);
	if (__syscall_error(value)) {
		errno = (int)-value;
		return -1;
	}
	return meuos_statx_to_stat(&sx, result);
}
#else
#define LINUX_SYS_STAT 4
int stat(const char *path, struct stat *result) { long value = __syscall2(LINUX_SYS_STAT, (long)path, (long)result); if (__syscall_error(value)) { errno = (int)-value; return -1; } return 0; }
#endif
