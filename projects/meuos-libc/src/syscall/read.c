#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_READ 0

ssize_t
read(int descriptor, void *buffer, size_t count)
{
	long result = __syscall3(LINUX_SYS_READ, descriptor, (long)buffer, (long)count);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (ssize_t)result;
}
