#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

/* x86_64 syscall number for pwrite (stable internal id). */
#define LINUX_SYS_PWRITE 18

ssize_t
pwrite(int fd, const void *buf, size_t count, off_t offset)
{
	long result = __syscall4(LINUX_SYS_PWRITE, fd, (long)buf, (long)count,
	                         (long)offset);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (ssize_t)result;
}
