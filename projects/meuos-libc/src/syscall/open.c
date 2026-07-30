#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <unistd.h>
#include "../internal/syscall.h"

#if defined(__aarch64__) || defined(__riscv) || defined(__loongarch64) || defined(__arm__)
/* aarch64 没有 open(2)，改用 openat(2)，AT_FDCWD 表示相对当前工作目录。 */
#define AT_FDCWD (-100)
#define LINUX_SYS_OPENAT 257
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
	result = __syscall4(LINUX_SYS_OPENAT, AT_FDCWD, (long)path, flags, mode);
	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (int)result;
}
#else
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
#endif
