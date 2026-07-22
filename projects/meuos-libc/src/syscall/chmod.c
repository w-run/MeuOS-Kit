#include <errno.h>
#include <sys/stat.h>
#include "../internal/syscall.h"
#if defined(__aarch64__)
/* aarch64 没有 chmod(2)，改用 fchmodat(AT_FDCWD, path, mode, 0)。 */
#define AT_FDCWD (-100)
#define LINUX_SYS_FCHMODAT 268
int chmod(const char *path, mode_t mode) {
	long value = __syscall4(LINUX_SYS_FCHMODAT, AT_FDCWD, (long)path, (long)mode, 0);
	if (__syscall_error(value)) { errno = (int)-value; return -1; }
	return 0;
}
#else
#define LINUX_SYS_CHMOD 90
int chmod(const char *path, mode_t mode) { long value = __syscall2(LINUX_SYS_CHMOD, (long)path, mode); if (__syscall_error(value)) { errno = (int)-value; return -1; } return 0; }
#endif
