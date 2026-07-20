#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_PIPE 22

int
pipe(int descriptors[2])
{
	long result = __syscall1(LINUX_SYS_PIPE, (long)descriptors);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}
