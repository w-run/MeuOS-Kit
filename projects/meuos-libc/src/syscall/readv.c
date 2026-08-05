#include <errno.h>
#include <sys/uio.h>
#include "../internal/syscall.h"

/* x86_64 syscall number for readv (stable internal id). */
#define LINUX_SYS_READV 19

ssize_t
readv(int fd, const struct iovec *iov, int iovcnt)
{
	long result = __syscall3(LINUX_SYS_READV, fd, (long)iov, iovcnt);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (ssize_t)result;
}
