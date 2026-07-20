#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

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
