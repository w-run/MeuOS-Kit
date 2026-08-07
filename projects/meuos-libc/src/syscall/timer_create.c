#include <errno.h>
#include <time.h>
#include <signal.h>
#include "../internal/syscall.h"

int
timer_create(clockid_t clock, struct sigevent *restrict evp,
             timer_t *restrict timerid)
{
	long r = __syscall3(222, clock, (long)evp, (long)timerid);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}