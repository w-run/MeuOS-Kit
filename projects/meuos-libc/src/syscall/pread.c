#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

/* x86_64 syscall number for pread (stable internal id). */
#define LINUX_SYS_PREAD 17

ssize_t
pread(int fd, void *buf, size_t count, off_t offset)
{
	long result = __syscall4(LINUX_SYS_PREAD, fd, (long)buf, (long)count,
	                         (long)offset);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (ssize_t)result;
}
