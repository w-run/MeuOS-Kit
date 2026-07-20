#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"
#define LINUX_SYS_FCHDIR 81
int fchdir(int fd) {
	long r = __syscall1(LINUX_SYS_FCHDIR, fd);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}
