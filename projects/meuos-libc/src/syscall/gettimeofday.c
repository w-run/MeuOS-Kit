#include <errno.h>
#include <stdint.h>
#include <sys/time.h>
#include "../internal/syscall.h"

#if defined(__i386__)
/* Legacy i386 gettimeofday (syscall 96) returns 32-bit time_t, which
 * mismatches our 64-bit time_t.  Source the value from clock_gettime64
 * (syscall 403) and synthesise the timeval. */
int
gettimeofday(struct timeval *tv, void *tz)
{
	struct { int64_t tv_sec; int64_t tv_nsec; } kernel_result;
	long value;

	if (tv) {
		value = __syscall6(403, 0 /* CLOCK_REALTIME */,
			(long)&kernel_result, 0, 0, 0, 0);
		if (__syscall_error(value)) {
			errno = (int)-value;
			return -1;
		}
		tv->tv_sec = (time_t)kernel_result.tv_sec;
		/* tv_nsec is always in [0, 999999999], so truncating to
		 * long (32-bit on i386) before dividing avoids the 64-bit
		 * division that mcc/i386 does not yet support. */
		tv->tv_usec = (suseconds_t)((long)kernel_result.tv_nsec / 1000);
	}
	(void)tz;  /* timezone is obsolete; always report zero. */
	return 0;
}
#else
#define LINUX_SYS_GETTIMEOFDAY 96
int gettimeofday(struct timeval *tv, void *tz) {
	long r = __syscall2(LINUX_SYS_GETTIMEOFDAY, (long)tv, (long)tz);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	return 0;
}
#endif
