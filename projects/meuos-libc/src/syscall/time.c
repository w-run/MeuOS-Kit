#include <errno.h>
#include <time.h>

/* time_t is explicitly 64-bit on i386.  Avoid the legacy i386 time(2)
 * syscall, whose return value is a 32-bit long, and source the value from
 * the time64 clock_gettime implementation instead. */
time_t
time(time_t *tloc)
{
	struct timespec now;

	if (clock_gettime(CLOCK_REALTIME, &now) < 0)
		return (time_t)-1;
	if (tloc)
		*tloc = now.tv_sec;
	return now.tv_sec;
}
