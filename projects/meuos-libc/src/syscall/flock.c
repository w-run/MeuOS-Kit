#include <errno.h>
#include <sys/file.h>
#include "../internal/syscall.h"
#define LINUX_SYS_FLOCK 73
int flock(int fd, int operation) {
	long r = __syscall2(LINUX_SYS_FLOCK, fd, operation);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}
