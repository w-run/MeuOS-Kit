#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include "../internal/syscall.h"

#ifndef AT_FDCWD
#define AT_FDCWD (-100)
#endif
#define LINUX_SYS_OPENAT 257

int
openat(int dirfd, const char *path, int flags, ...)
{
	va_list args;
	mode_t mode = 0;
	long result;

	if (flags & O_CREAT) {
		va_start(args, flags);
		mode = va_arg(args, mode_t);
		va_end(args);
	}
	result = __syscall4(LINUX_SYS_OPENAT, dirfd, (long)path, flags, mode);
	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (int)result;
}
