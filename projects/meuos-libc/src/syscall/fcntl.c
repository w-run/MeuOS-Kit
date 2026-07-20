#include <errno.h>
#include <stdarg.h>
#include <fcntl.h>
#include "../internal/syscall.h"
#define LINUX_SYS_FCNTL 72
int fcntl(int fd, int cmd, ...) {
	long arg = 0;
	if (cmd != F_GETFD && cmd != F_GETFL) {
		va_list ap; va_start(ap, cmd); arg = va_arg(ap, long); va_end(ap);
	}
	long r = __syscall3(LINUX_SYS_FCNTL, fd, cmd, arg);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return (int)r;
}
