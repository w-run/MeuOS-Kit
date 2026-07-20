#include <utime.h>
#include <errno.h>
#include <time.h>
#include "../internal/syscall.h"

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
