#include <errno.h>
#include <unistd.h>
#include "../internal/syscall.h"

#if defined(__aarch64__)
/* aarch64 没有 pipe(22)，改用 pipe2(fds, flags)。flags=0 等价 pipe()。 */
#define LINUX_SYS_PIPE2 293

int
pipe(int descriptors[2])
{
	long result = __syscall2(LINUX_SYS_PIPE2, (long)descriptors, 0);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return 0;
}
#else
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
#endif
