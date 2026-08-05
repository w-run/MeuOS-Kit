/* time/alarm.c — POSIX.1-2008 scheduling alarms (alarm).
 *
 * alarm(seconds) arranges for a SIGALRM to be delivered to the calling
 * process after seconds real seconds, and returns the number of seconds
 * previously remaining on an active alarm (0 if none).  A second call to
 * alarm() replaces the pending timer.  alarm(0) cancels any pending alarm.
 *
 * The asm-generic kernels (aarch64/riscv/loongarch64) expose no standalone
 * alarm(2), so this is implemented uniformly on every architecture as a
 * one-shot ITIMER_REAL via setitimer() — the kernel raises SIGALRM itself on
 * expiry, matching alarm(2)'s observable behaviour.  The previous countdown
 * the old timer had remaining is read back from the replaced itimerval.
 * Zero GNU dependency. */

#include <unistd.h>
#include <sys/time.h>

unsigned int
alarm(unsigned int seconds)
{
	struct itimerval it = { 0 }, old;

	it.it_value.tv_sec = (time_t)seconds;
	it.it_value.tv_usec = 0;

	if (setitimer(ITIMER_REAL, &it, &old) != 0)
		return 0;                    /* no prior alarm on failure */

	/* Convert the leftover interval to whole seconds (round the partial
	 * microsecond remainder up, matching Linux alarm(2)). */
	return (unsigned int)old.it_value.tv_sec
	    + (old.it_value.tv_usec != 0 ? 1 : 0);
}
