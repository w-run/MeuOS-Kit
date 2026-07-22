#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#if defined(__aarch64__)
/* aarch64 没有 mkdir(2)，改用 mkdirat(AT_FDCWD, path, mode)。 */
#define AT_FDCWD (-100)
#define LINUX_SYS_MKDIRAT 258
int
mkdir(const char *path, mode_t mode)
{
	long result = __syscall3(LINUX_SYS_MKDIRAT, AT_FDCWD, (long)path, mode);
	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}
#else
#define LINUX_SYS_MKDIR 83

int
mkdir(const char *path, mode_t mode)
{
	long result = __syscall2(LINUX_SYS_MKDIR, (long)path, mode);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}
#endif
