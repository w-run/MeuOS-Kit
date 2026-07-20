#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_CLOSE 3

int
close(int descriptor)
{
	long result = __syscall1(LINUX_SYS_CLOSE, descriptor);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (int)result;
}
