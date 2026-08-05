#include <errno.h>
#include <sys/uio.h>
#include "../internal/syscall.h"

/* x86_64 syscall number for writev (stable internal id). */
#define LINUX_SYS_WRITEV 20

ssize_t
writev(int fd, const struct iovec *iov, int iovcnt)
{
	long result = __syscall3(LINUX_SYS_WRITEV, fd, (long)iov, iovcnt);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (ssize_t)result;
}
