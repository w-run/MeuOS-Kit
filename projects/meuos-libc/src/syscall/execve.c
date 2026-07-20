#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_EXECVE 59

int
execve(const char *path, char *const arguments[], char *const environment[])
{
	long result = __syscall3(LINUX_SYS_EXECVE, (long)path, (long)arguments, (long)environment);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (int)result;
}
