#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_LSEEK 8

off_t
lseek(int descriptor, off_t offset, int origin)
{
	long result = __syscall3(LINUX_SYS_LSEEK, descriptor, offset, origin);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (off_t)result;
}
