#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_FORK 57

pid_t
fork(void)
{
	long result = __syscall0(LINUX_SYS_FORK);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (pid_t)result;
}
