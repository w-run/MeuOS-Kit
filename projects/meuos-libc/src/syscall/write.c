#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

/* Linux x86_64 syscall number for write. */
#define LINUX_SYS_WRITE 1

ssize_t
write(int descriptor, const void *buffer, size_t count)
{
	long result = __syscall3(LINUX_SYS_WRITE, descriptor, (long)buffer, (long)count);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (ssize_t)result;
}
