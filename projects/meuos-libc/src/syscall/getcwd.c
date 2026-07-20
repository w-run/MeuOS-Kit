#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#define LINUX_SYS_GETCWD 79

char *
getcwd(char *buffer, size_t size)
{
	long result = __syscall2(LINUX_SYS_GETCWD, (long)buffer, (long)size);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return 0;
	}
	return buffer;
}
