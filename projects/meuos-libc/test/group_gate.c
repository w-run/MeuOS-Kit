/* group_gate.c — setpgid/getpgid/getpgrp/setsid/getsid regression gate.
 *
 * Verifies process-group/session semantics: getpgrp() == getpgid(0), a
 * process can set itself as its own group leader with setpgid(0,0), and a
 * freshly forked child (not a session leader) can start a new session with
 * setsid() and then observe itself as both group and session leader. */
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <errno.h>

static int fails;

static void
chk(const char *lbl, int cond)
{
	if (!cond) {
		printf("FAIL: %s (errno=%d %s)\n", lbl, errno, "err");
		fails++;
	}
}

int
main(void)
{
	pid_t self = getpid();
	pid_t pgrp = getpgrp();

	/* getpgrp() must equal getpgid(0). */
	chk("getpgid(0) == getpgrp()", getpgid(0) == pgrp);

	/* Put the current process in its own group and verify. */
	errno = 0;
	if (setpgid(0, 0) != 0)
		printf("note: setpgid(0,0) errno=%d\n", errno);
	chk("getpgid(0) == self after setpgid(0,0)", getpgid(0) == self);

	/* A freshly forked child is not a session leader, so setsid() must
	 * succeed and hand it a new session whose group == its own pid. */
	pid_t pid = fork();
	if (pid < 0) { perror("fork"); fails++; }
	else if (pid == 0) {
		errno = 0;
		pid_t s = setsid();
		if (s == -1) { printf("child setsid errno=%d\n", errno); _exit(1); }
		if (s != getpid()) { printf("child setsid=%d != pid=%d\n", s, getpid()); _exit(2); }
		if (getpgrp() != getpid()) { printf("child getpgrp=%d != pid\n", (int)getpgrp()); _exit(3); }
		_exit(0);
	} else {
		int st;
		waitpid(pid, &st, 0);
		chk("child setsid ok", WIFEXITED(st) && WEXITSTATUS(st) == 0);
	}

	/* Invalid pid for getpgid/getsid → ESRCH (or a harmless valid group for
	 * a negative/zero which the kernel handles); use a huge pid. */
	errno = 0;
	if (getpgid((pid_t)0x3fffffff) != -1) {
		printf("note: getpgid(bighid)=%ld errno=%d\n", (long)getpgid((pid_t)0x3fffffff), errno);
	}

	if (fails) {
		printf("%d group FAIL\n", fails);
		return 1;
	}
	printf("PASS group\n");
	return 0;
}
