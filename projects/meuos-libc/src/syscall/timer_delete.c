#include <errno.h>
#include <time.h>
#include "../internal/syscall.h"

int
timer_delete(timer_t timerid)
{
	long r = __syscall1(226, timerid);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}