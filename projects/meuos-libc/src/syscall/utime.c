#include <utime.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include "../internal/syscall.h"

#if defined(__i386__)
/* Legacy i386 utime (syscall 132) takes 32-bit time_t.  Use
 * utimensat_time64 (syscall 412) which accepts 64-bit timestamps.
 * utimensat(dirfd, path, times[2], flags) with AT_FDCWD and 0 flags
 * matches utime() semantics when both times are non-NULL. */
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
