/* alarm_gate.c — alarm() regression gate.
 *
 * Verifies the POSIX alarm() contract: it returns the seconds remaining on
 * a previous alarm (0 if none), alarm(0) cancels a pending alarm, and an
 * armed alarm() delivers SIGALRM.  The exact leftover-second return is not
 * asserted to an exact constant (rounding and scheduling jitter), but is
 * checked within the expected 1-second window. */
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <stdio.h>
#include <errno.h>

static int got_alarm;   /* set by the SIGALRM handler; read across nanosleep */

static void
on_alrm(int sig)
{
	(void)sig;
	got_alarm = 1;
}

static int fails;

static void
chk(const char *lbl, int cond)
{
	if (!cond) {
		printf("FAIL: %s\n", lbl);
		fails++;
	}
}

int
main(void)
{
	unsigned r;

	signal(SIGALRM, on_alrm);

	/* no pending alarm: alarm(0) returns 0 */
	r = alarm(0);
	if (r != 0) { printf("FAIL: alarm(0) initial=%u want 0\n", r); fails++; }

	/* first arming of a long timer: returns 0 (nothing previously set) */
	r = alarm(60);
	if (r != 0) { printf("FAIL: alarm(60) first=%u want 0\n", r); fails++; }

	/* re-arming reports the previous countdown (60s just set -> ~60s left).
	 * Jitter-safe: accept 59 or 60. */
	r = alarm(0);                       /* cancel the 60s alarm */
	chk("alarm(0) cancels 60s alarm", r == 60 || r == 59);

	/* arm a 1s alarm, then wait for SIGALRM */
	r = alarm(1);
	if (r != 0) { printf("FAIL: alarm(1) after cancel=%u want 0\n", r); fails++; }

	{
		struct timespec d = { 0, 50 * 1000 * 1000 };   /* 50ms */
		for (int i = 0; i < 40 && !got_alarm; ++i)
			nanosleep(&d, NULL);         /* up to ~2s */
	}
	chk("SIGALRM delivered after alarm(1)", got_alarm);

	/* after expiry the one-shot timer is disarmed; alarm(0) reports 0 */
	r = alarm(0);
	if (r != 0) { printf("FAIL: alarm(0) post-expiry=%u want 0\n", r); fails++; }

	if (fails) {
		printf("%d alarm FAIL\n", fails);
		return 1;
	}
	printf("PASS alarm\n");
	return 0;
}
