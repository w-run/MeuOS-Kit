#include <utime.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include "../internal/syscall.h"

#if defined(__i386__) || defined(__aarch64__) || defined(__riscv) || defined(__loongarch64)
/* i386: legacy utime (syscall 132) 只接受 32 位 time_t，与本库 64 位
 *   time_t 不兼容，改用 utimensat_time64(412) 取 64 位时间戳。
 * aarch64: 没有 utime(132)，直接用 utimensat（x86_64 内部号 412，
 *   翻译表转 aarch64 88）。
 * utimensat(dirfd, path, times[2], flags) 配 AT_FDCWD + 0 flags，且 times
 * 非 NULL 时与 utime() 语义等价。 */
#define MEUOS_AT_FDCWD (-100)
#define LINUX_SYS_UTIMENSAT_TIME64 412

int
utime(const char *filename, const struct utimbuf *times)
{
	struct { int64_t tv_sec; int64_t tv_nsec; } ts64[2];
	long result;

	if (times) {
		ts64[0].tv_sec = times->actime;
		ts64[0].tv_nsec = 0;
		ts64[1].tv_sec = times->modtime;
		ts64[1].tv_nsec = 0;
		result = __syscall6(LINUX_SYS_UTIMENSAT_TIME64, MEUOS_AT_FDCWD,
			(long)filename, (long)ts64, 0, 0, 0);
	} else {
		/* Passing NULL times to utimensat sets both times to current. */
		result = __syscall6(LINUX_SYS_UTIMENSAT_TIME64, MEUOS_AT_FDCWD,
			(long)filename, 0, 0, 0, 0);
	}
	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}
#else
/* x86_64 syscall number for utime. */
#define LINUX_SYS_UTIME 132

int
utime(const char *filename, const struct utimbuf *times)
{
	long result = __syscall2(LINUX_SYS_UTIME, (long)filename, (long)times);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}
#endif
