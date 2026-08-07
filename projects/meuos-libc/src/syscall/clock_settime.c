#include <errno.h>
#include <time.h>
#include "../internal/syscall.h"

int
clock_settime(clockid_t clock, const struct timespec *tp)
{
	long r = __syscall2(227, clock, (long)tp);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}