#include <errno.h>
#include <time.h>
#include "../internal/syscall.h"

int
timer_settime(timer_t timerid, int flags,
              const struct itimerspec *restrict value,
              struct itimerspec *restrict ovalue)
{
	long r = __syscall4(223, timerid, flags, (long)value, (long)ovalue);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}