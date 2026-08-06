#include <errno.h>
#include <stdint.h>
#include <time.h>
#include "../internal/syscall.h"

int
clock_getres(clockid_t clock, struct timespec *result)
{
	long value = __syscall2(229, clock, (long)result);
	if (__syscall_error(value)) {
		errno = (int)-value;
		return -1;
	}
	return 0;
}