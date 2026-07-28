#include <errno.h>
#include <sys/ioctl.h>
#include "../internal/syscall.h"

#define LINUX_SYS_IOCTL 16

int
ioctl(int fd, unsigned long request, ...)
{
	va_list ap;
	void *arg;
	long result;

	va_start(ap, request);
	arg = va_arg(ap, void *);
	va_end(ap);

	result = __syscall3(LINUX_SYS_IOCTL, fd, request, (long)arg);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (int)result;
}
