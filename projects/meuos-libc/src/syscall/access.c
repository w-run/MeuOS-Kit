#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"
#if defined(__aarch64__)
/* aarch64 没有 access(2)，改用 faccessat(AT_FDCWD, path, mode, 0)。 */
#define AT_FDCWD (-100)
#define LINUX_SYS_FACCESSAT 269
int access(const char *path, int mode) {
	long value = __syscall4(LINUX_SYS_FACCESSAT, AT_FDCWD, (long)path, mode, 0);
	if (__syscall_error(value)) { errno = (int)-value; return -1; }
	return 0;
}
#else
#define LINUX_SYS_ACCESS 21
int access(const char *path, int mode) { long value = __syscall2(LINUX_SYS_ACCESS, (long)path, mode); if (__syscall_error(value)) { errno = (int)-value; return -1; } return 0; }
#endif
