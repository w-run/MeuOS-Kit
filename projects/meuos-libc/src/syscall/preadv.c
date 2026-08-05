#include <errno.h>
#include <sys/uio.h>
#include "../internal/syscall.h"

/* x86_64 syscall number for preadv (stable internal id). */
#define LINUX_SYS_PREADV 295

ssize_t
preadv(int fd, const struct iovec *iov, int iovcnt, off_t offset)
{
	long result = __syscall4(LINUX_SYS_PREADV, fd, (long)iov, iovcnt,
	                         (long)offset);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (ssize_t)result;
}
