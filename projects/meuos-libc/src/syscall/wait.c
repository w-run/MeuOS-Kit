#include <errno.h>
#include <sys/wait.h>
#include "../internal/syscall.h"

/* POSIX wait() over Linux wait4 with a null rusage pointer. */
#define LINUX_SYS_WAIT4 61

pid_t
wait(int *status)
{
	long result = __syscall4(LINUX_SYS_WAIT4, (long)-1, (long)status, 0, 0);
	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (pid_t)result;
}
