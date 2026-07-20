#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

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
