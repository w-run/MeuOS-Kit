#include <errno.h>
#include <sys/uio.h>
#include "../internal/syscall.h"

/* x86_64 syscall number for pwritev (stable internal id). */
#define LINUX_SYS_PWRITEV 296

ssize_t
pwritev(int fd, const struct iovec *iov, int iovcnt, off_t offset)
{
	long result = __syscall4(LINUX_SYS_PWRITEV, fd, (long)iov, iovcnt,
	                         (long)offset);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (ssize_t)result;
}
