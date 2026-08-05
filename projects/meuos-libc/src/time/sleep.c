/* time/sleep.c — POSIX.1-2008 suspending execution (sleep/usleep).
 *
 * sleep(sec) suspends the calling thread for sec whole seconds, returning the
 * number of seconds of sleep still remaining (i.e. when interrupted by a
 * signal that was caught); sleep(0) simply returns 0.  usleep(usec) suspends
 * for usec microseconds (clamped to < 1s) and returns 0, or -1 on error.
 * Both build on the existing nanosleep().  Zero GNU dependency.
 */

#include <unistd.h>
#include <time.h>
#include <errno.h>

unsigned int
sleep(unsigned int seconds)
{
	struct timespec req;
	struct timespec rem;

	req.tv_sec = seconds;
	req.tv_nsec = 0;

	if (nanosleep(&req, &rem) == 0)
		return 0;
	if (errno == EINTR)
		return rem.tv_sec + (rem.tv_nsec > 0 ? 1 : 0);
	return seconds;              /* other error: report the full amount */
}

int
usleep(unsigned int usec)
{
	struct timespec req;

	/* clamp a >1s request to a single (sub-second) interval, and a zero
	 * request to a tiny yield. */
	if (usec >= 1000000)
		usec = 999999;

	req.tv_sec = 0;
	req.tv_nsec = (long)usec * 1000;

	if (nanosleep(&req, NULL) != 0)
		return -1;
	return 0;
}
