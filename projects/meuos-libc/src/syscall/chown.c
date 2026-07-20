#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include "../internal/syscall.h"
#define LINUX_SYS_CHOWN 92
int chown(const char *path, uid_t owner, gid_t group) {
	long r = __syscall3(LINUX_SYS_CHOWN, (long)path, (long)owner, (long)group);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}
