#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "../internal/syscall.h"
#define LINUX_SYS_WAIT4 61
pid_t wait4(pid_t pid, int *status, int options, void *rusage) {
	long r = __syscall4(LINUX_SYS_WAIT4, pid, (long)status, options, (long)rusage);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return (pid_t)r;
}
