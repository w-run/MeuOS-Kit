#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#if defined(__aarch64__) || defined(__riscv) || defined(__loongarch64)
/* aarch64 没有 rmdir(2)，改用 unlinkat(AT_FDCWD, path, AT_REMOVEDIR)。 */
#define AT_FDCWD (-100)
#define AT_REMOVEDIR 0x200
#define LINUX_SYS_UNLINKAT 263
int
rmdir(const char *path)
{
	long result = __syscall3(LINUX_SYS_UNLINKAT, AT_FDCWD, (long)path, AT_REMOVEDIR);
	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}
#else
#define LINUX_SYS_RMDIR 84

int
rmdir(const char *path)
{
	long result = __syscall1(LINUX_SYS_RMDIR, (long)path);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}
#endif
