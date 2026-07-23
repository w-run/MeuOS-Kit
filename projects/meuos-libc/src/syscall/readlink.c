#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"
#if defined(__aarch64__) || defined(__riscv) || defined(__loongarch64)
/* aarch64 没有 readlink(2)，改用 readlinkat(AT_FDCWD, path, buf, size)。 */
#define AT_FDCWD (-100)
#define LINUX_SYS_READLINKAT 267
ssize_t readlink(const char *path, char *buffer, size_t size) {
	long value = __syscall4(LINUX_SYS_READLINKAT, AT_FDCWD, (long)path, (long)buffer, (long)size);
	if (__syscall_error(value)) { errno = (int)-value; return -1; }
	return (ssize_t)value;
}
#else
#define LINUX_SYS_READLINK 89
ssize_t readlink(const char *path, char *buffer, size_t size) { long value = __syscall3(LINUX_SYS_READLINK, (long)path, (long)buffer, size); if (__syscall_error(value)) { errno = (int)-value; return -1; } return (ssize_t)value; }
#endif
