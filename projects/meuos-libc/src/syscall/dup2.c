#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_DUP2 33

int
dup2(int old_descriptor, int new_descriptor)
{
	long result = __syscall2(LINUX_SYS_DUP2, old_descriptor, new_descriptor);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (int)result;
}
