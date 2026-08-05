/* pause_gate.c — pause() regression gate.
 * Forks a child that waits briefly then sends SIGUSR1 to the parent.  The
 * parent installs a SIGUSR1 handler (no-op) and calls pause(); verifies the
 * call returns -1 with errno=EINTR within a short elapsed window. */
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>

static void
sig_usr1(int sig)
{
	(void)sig;
}

int
main(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = sig_usr1;
	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGUSR1, &sa, NULL) != 0) {
		printf("FAIL: sigaction %s\n", strerror(errno));
		return 1;
	}

	pid_t ppid = getpid();
	pid_t pid = fork();
	if (pid < 0) { perror("fork"); return 1; }
	if (pid == 0) {
		/* child: give the parent time to enter pause, then send SIGUSR1 */
		usleep(50000);                 /* 50ms */
		kill(ppid, SIGUSR1);
		_exit(0);
	}

	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	int r = pause();
	int e = errno;
	clock_gettime(CLOCK_MONOTONIC, &t1);

	/* wait for child to avoid zombies */
	int status;
	waitpid(pid, &status, 0);

	long long ms = (long long)(t1.tv_sec - t0.tv_sec) * 1000
	             + (long long)(t1.tv_nsec - t0.tv_nsec) / 1000000;

	if (r != -1) { printf("FAIL: pause returned %d want -1\n", r); return 1; }
	if (e != EINTR) { printf("FAIL: pause errno=%d want EINTR\n", e); return 1; }
	/* should wake ~50ms after child; generous lower bound 10ms */
	if (ms < 10) { printf("FAIL: pause elapsed=%lldms want>=10\n", ms); return 1; }
	/* and not absurdly long (no spurious early return) */
	if (ms > 5000) { printf("FAIL: pause elapsed=%lldms too long\n", ms); return 1; }

	printf("PASS pause\n");
	return 0;
}
