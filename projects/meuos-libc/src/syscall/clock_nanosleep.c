#include <errno.h>
#include <time.h>
#include "../internal/syscall.h"

int
clock_nanosleep(clockid_t clock, int flags,
                const struct timespec *req, struct timespec *rem)
{
	long r = __syscall4(230, clock, flags, (long)req, (long)rem);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}