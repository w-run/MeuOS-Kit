#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_PIPE2 293

int
pipe2(int pipefd[2], int flags)
{
	long result = __syscall2(LINUX_SYS_PIPE2, (long)pipefd, flags);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}
