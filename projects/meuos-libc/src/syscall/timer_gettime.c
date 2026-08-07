#include <errno.h>
#include <time.h>
#include "../internal/syscall.h"

int
timer_gettime(timer_t timerid, struct itimerspec *value)
{
	long r = __syscall2(224, timerid, (long)value);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}