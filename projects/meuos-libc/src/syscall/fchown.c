#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include "../internal/syscall.h"
#define LINUX_SYS_FCHOWN 93
int fchown(int fd, uid_t owner, gid_t group) {
	long r = __syscall3(LINUX_SYS_FCHOWN, fd, (long)owner, (long)group);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}
