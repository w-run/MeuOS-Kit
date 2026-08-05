/* time/itimer.c — POSIX interval timers (getitimer/setitimer).
 *
 * getitimer(which, value) reports the current countdown; setitimer(which,
 * new, old) arms or cancels a per-process interval timer which raises the
 * matching signal (SIGALRM for ITIMER_REAL, SIGVTALRM for ITIMER_VIRTUAL,
 * SIGPROF for ITIMER_PROF) on expiry.
 *
 * The kernel expects an "old" struct timeval whose fields are the C long
 * type (8 bytes on LP64, 4 on 32-bit), whereas our public timeval uses
 * suseconds_t (int32_t for tv_usec).  Passing the user struct straight
 * through would let the kernel read the uninitialised high 4 bytes of
 * tv_usec as part of a long on LP64.  We therefore bounce through a
 * kernel-layout struct and convert, mirroring gettimeofday.  Zero GNU
 * dependency. */

#include <sys/time.h>
#include <errno.h>
#include "../internal/syscall.h"

/* Kernel-layout interval timer: both fields are the C long type. */
struct k_timeval {
	long tv_sec;
	long tv_usec;
};

struct k_itimerval {
	struct k_timeval it_interval;
	struct k_timeval it_value;
};

/* x86_64 native syscall numbers (internal stable ids); mapped per-arch in
 * internal/syscall.h. */
#ifndef LINUX_SYS_GETITIMER
#define LINUX_SYS_GETITIMER 36
#endif
#ifndef LINUX_SYS_SETITIMER
#define LINUX_SYS_SETITIMER 38
#endif

static void
to_kernel(const struct itimerval *in, struct k_itimerval *out)
{
	out->it_interval.tv_sec  = in->it_interval.tv_sec;
	out->it_interval.tv_usec = in->it_interval.tv_usec;
	out->it_value.tv_sec     = in->it_value.tv_sec;
	out->it_value.tv_usec    = in->it_value.tv_usec;
}

static void
to_user(const struct k_itimerval *in, struct itimerval *out)
{
	out->it_interval.tv_sec  = (time_t)in->it_interval.tv_sec;
	out->it_interval.tv_usec = (suseconds_t)in->it_interval.tv_usec;
	out->it_value.tv_sec     = (time_t)in->it_value.tv_sec;
	out->it_value.tv_usec    = (suseconds_t)in->it_value.tv_usec;
}

int
getitimer(int which, struct itimerval *value)
{
	struct k_itimerval kv;
	long r = __syscall2(LINUX_SYS_GETITIMER, which, (long)&kv);
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	to_user(&kv, value);
	return 0;
}

int
setitimer(int which, const struct itimerval *newval, struct itimerval *oldval)
{
	struct k_itimerval kn, ko;
	if (newval)
		to_kernel(newval, &kn);
	/* Zero-init so the kernel never reads garbage padding. */
	ko.it_interval.tv_sec = ko.it_interval.tv_usec = 0;
	ko.it_value.tv_sec = ko.it_value.tv_usec = 0;

	long r = __syscall3(LINUX_SYS_SETITIMER, which,
	    (long)(newval ? &kn : 0), (long)(oldval ? &ko : 0));
	if (__syscall_error(r)) { errno = (int)-r; return -1; }
	if (oldval)
		to_user(&ko, oldval);
	return 0;
}
