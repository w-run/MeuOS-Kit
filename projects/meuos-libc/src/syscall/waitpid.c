#include <errno.h>
#include <sys/wait.h>
#include "../internal/syscall.h"

/* waitpid is the POSIX facade over Linux wait4 with a null rusage pointer. */
#define LINUX_SYS_WAIT4 61

pid_t
waitpid(pid_t process, int *status, int options)
{
	long result = __syscall4(LINUX_SYS_WAIT4, process, (long)status, options, 0);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (pid_t)result;
}
