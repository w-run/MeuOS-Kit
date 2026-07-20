#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include "../internal/syscall.h"

#define LINUX_SYS_FCHMOD 91

int
fchmod(int fd, mode_t mode)
{
	long result = __syscall2(LINUX_SYS_FCHMOD, fd, (long)mode);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}
