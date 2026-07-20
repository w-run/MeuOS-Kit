#include <errno.h>
#include <time.h>
#include "../internal/syscall.h"

#ifndef LINUX_SYS_TIME
#define LINUX_SYS_TIME 201
#endif

time_t
time(time_t *tloc)
{
	/* The Linux time() syscall returns seconds since epoch in the
	 * tloc pointer (or 0 on success and tloc == NULL on failure). */
	long r = __syscall1(LINUX_SYS_TIME, (long)tloc);
	if (r < 0) {
		errno = (int)-r;
		return (time_t)-1;
	}
	if (tloc && r == 0)
		return *tloc;
	return (time_t)r;
}
