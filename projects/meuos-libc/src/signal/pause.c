/* signal/pause.c — POSIX pause() suspend until signal.
 *
 * pause() blocks the calling thread until a signal is delivered and caught
 * (or terminates the process).  Implemented via rt_sigsuspend with an empty
 * signal mask so any unblocked signal is delivered.  Always returns -1
 * with errno=EINTR on return.  Zero GNU dependency; pause is POSIX.1-2008
 * in core libc. */

#include <signal.h>
#include <errno.h>
#include <unistd.h>

int
pause(void)
{
	sigset_t empty;
	sigemptyset(&empty);
	/* sigsuspend unblocks empty mask (== block everything) and waits for a
	 * signal -- on delivery it restores the previous (empty) mask and
	 * returns -1 with EINTR.  pause returns directly on delivery. */
	if (sigsuspend(&empty) == -1 && errno == EINTR)
		return -1;
	/* sigsuspend returns -1 + EINTR on wake; we pass through that. */
	return -1;
}
