#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"
#if defined(__aarch64__)
/* aarch64 没有 symlink(2)，改用 symlinkat(target, AT_FDCWD, path)。 */
#define AT_FDCWD (-100)
#define LINUX_SYS_SYMLINKAT 266
int symlink(const char *target, const char *path) {
	long value = __syscall3(LINUX_SYS_SYMLINKAT, (long)target, AT_FDCWD, (long)path);
	if (__syscall_error(value)) { errno = (int)-value; return -1; }
	return 0;
}
#else
#define LINUX_SYS_SYMLINK 88
int symlink(const char *target, const char *path) { long value = __syscall2(LINUX_SYS_SYMLINK, (long)target, (long)path); if (__syscall_error(value)) { errno = (int)-value; return -1; } return 0; }
#endif
