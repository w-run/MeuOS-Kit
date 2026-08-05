/* getppid_gate.c — getppid() regression gate (incl. cross-arch syscall
 * mapping fix: x86_64 internal id 110 must translate to i386/ARM 64 and
 * asm-generic 173, not pass through 110 on non-x86_64).
 *
 * Verifies getppid() returns a live positive id in the parent, and that a
 * just-forked child sees the parent's pid as its own ppid. */
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <errno.h>

static int fails;

int
main(void)
{
	pid_t ppid, self, child;

	/* parent: getppid is positive (the shell/init parent exists) */
	ppid = getppid();
	self = getpid();
	if (ppid <= 0) {
		printf("FAIL: getppid=%d not positive\n", (int)ppid);
		fails++;
	}

	child = fork();
	if (child < 0) { perror("fork"); fails++; }
	else if (child == 0) {
		/* child: ppid must be the parent (our own) pid */
		pid_t cpp = getppid();
		if (cpp == self) { _exit(0); }
		printf("child getppid=%d != parent %d\n", (int)cpp, (int)self);
		_exit(1);
	} else {
		int st;
		waitpid(child, &st, 0);
		if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
			printf("FAIL: child ppid mismatch\n");
			fails++;
		}
	}

	if (fails) {
		printf("%d getppid FAIL\n", fails);
		return 1;
	}
	printf("PASS getppid\n");
	return 0;
}
