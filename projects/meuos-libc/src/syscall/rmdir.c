#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

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
