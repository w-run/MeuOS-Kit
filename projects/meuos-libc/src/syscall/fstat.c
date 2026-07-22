#include <errno.h>
#include <sys/stat.h>
#include "../internal/syscall.h"

#if defined(__i386__)
#include "../internal/arch/i386/statx.h"
int
fstat(int descriptor, struct stat *result)
{
	struct meuos_statx sx;
	long value = __syscall6(LINUX_SYS_STATX_I386, descriptor,
		(long)"", MEUOS_AT_EMPTY_PATH,
		MEUOS_STATX_BASIC_STATS, (long)&sx, 0);
	if (__syscall_error(value)) {
		errno = (int)-value;
		return -1;
	}
	return meuos_statx_to_stat(&sx, result);
}
#elif defined(__aarch64__)
/* aarch64 用 statx + AT_EMPTY_PATH 复刻 fstat() 语义。AT_EMPTY_PATH 需要
 * descriptor 指向打开的文件；空路径 "" 是 statx 的约定。 */
#include "../internal/arch/i386/statx.h"
int
fstat(int descriptor, struct stat *result)
{
	struct meuos_statx sx;
	long value = __syscall6(LINUX_SYS_STATX, descriptor,
		(long)"", MEUOS_AT_EMPTY_PATH,
		MEUOS_STATX_BASIC_STATS, (long)&sx, 0);
	if (__syscall_error(value)) {
		errno = (int)-value;
		return -1;
	}
	return meuos_statx_to_stat(&sx, result);
}
#else
#define LINUX_SYS_FSTAT 5
int fstat(int descriptor, struct stat *result) { long value = __syscall2(LINUX_SYS_FSTAT, descriptor, (long)result); if (__syscall_error(value)) { errno = (int)-value; return -1; } return 0; }
#endif
