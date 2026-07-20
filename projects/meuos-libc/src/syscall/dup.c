#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_DUP 32

int
dup(int descriptor)
{
	long result = __syscall1(LINUX_SYS_DUP, descriptor);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (int)result;
}
