#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_OPEN 2

int
open(const char *path, int flags, ...)
{
	va_list arguments;
	mode_t mode = 0;
	long result;

	if (flags & O_CREAT) {
		va_start(arguments, flags);
		mode = va_arg(arguments, mode_t);
		va_end(arguments);
	}
	result = __syscall3(LINUX_SYS_OPEN, (long)path, flags, mode);
	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (int)result;
}
