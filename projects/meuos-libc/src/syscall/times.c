#include <sys/times.h>
#include <errno.h>
#include "../internal/syscall.h"

#define LINUX_SYS_TIMES 100

clock_t
times(struct tms *buf)
{
	long result = __syscall1(LINUX_SYS_TIMES, (long)buf);

	if (__syscall_error(result)) {
		errno = (int)-result;
		return -1;
	}
	return (clock_t)result;
}
