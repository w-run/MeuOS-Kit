#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#if defined(__aarch64__) || defined(__riscv) || defined(__loongarch64) || defined(__arm__)
/* aarch64 没有 unlink(2)，改用 unlinkat(AT_FDCWD, path, 0)。 */
#define AT_FDCWD (-100)
#define LINUX_SYS_UNLINKAT 263
int
unlink(const char *path)
{
	long result = __syscall3(LINUX_SYS_UNLINKAT, AT_FDCWD, (long)path, 0);
	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}
#else
#define LINUX_SYS_UNLINK 87

int
unlink(const char *path)
{
	long result = __syscall1(LINUX_SYS_UNLINK, (long)path);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}
#endif
