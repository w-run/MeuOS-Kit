#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_DUP3 292

int
dup3(int oldfd, int newfd, int flags)
{
	long result = __syscall3(LINUX_SYS_DUP3, oldfd, newfd, flags);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (int)result;
}
