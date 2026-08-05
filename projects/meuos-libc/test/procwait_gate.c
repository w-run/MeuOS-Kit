/* procwait_gate.c — fork/wait/waitpid/_exit/exit status fine-grained gate.
 *
 * Verifies the process-wait lifecycle with precise exit-status assertions:
 * a child's _exit(N) is observed via WIFEXITED/WEXITSTATUS, a C-library
 * exit(N) likewise, a signal-terminated child via WIFSIGNALED/WTERMSIG,
 * and waitpid returns the reaped child's pid.  Isolates process semantics
 * that a coarse process.c would mask. */
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>

static int fails;

static void
chk(const char *lbl, int cond)
{
	if (!cond) {
		printf("FAIL: %s (errno=%d)\n", lbl, errno);
		fails++;
	}
}

int
main(void)
{
	pid_t pid, r;
	int status;

	/* child _exit(7) → WIFEXITED + WEXITSTATUS==7 */
	pid = fork();
	if (pid < 0) { perror("fork1"); fails++; }
	else if (pid == 0) {
		_exit(7);
	} else {
		r = waitpid(pid, &status, 0);
		chk("waitpid returns child", r == pid);
		chk("WIFEXITED(7)", WIFEXITED(status));
		chk("WEXITSTATUS==7", WEXITSTATUS(status) == 7);
	}

	/* child exit(3) via the C library */
	pid = fork();
	if (pid < 0) { perror("fork2"); fails++; }
	else if (pid == 0) {
		exit(3);
	} else {
		r = waitpid(pid, &status, 0);
		chk("waitpid2 returns child", r == pid);
		chk("WIFEXITED(exit3)", WIFEXITED(status));
		chk("WEXITSTATUS==3", WEXITSTATUS(status) == 3);
	}

	/* boundary exit codes 0 and 255 survive the status encoding */
	pid = fork();
	if (pid < 0) { perror("fork0"); fails++; }
	else if (pid == 0) {
		_exit(0);
	} else {
		waitpid(pid, &status, 0);
		chk("WIFEXITED(_exit 0)", WIFEXITED(status));
		chk("WEXITSTATUS==0", WEXITSTATUS(status) == 0);
	}
	pid = fork();
	if (pid < 0) { perror("fork255"); fails++; }
	else if (pid == 0) {
		_exit(255);
	} else {
		waitpid(pid, &status, 0);
		chk("WIFEXITED(_exit 255)", WIFEXITED(status));
		chk("WEXITSTATUS==255", WEXITSTATUS(status) == 255);
	}

	/* child killed by SIGKILL → WIFSIGNALED + WTERMSIG==SIGKILL */
	pid = fork();
	if (pid < 0) { perror("fork3"); fails++; }
	else if (pid == 0) {
		pause();               /* wait to be killed */
		_exit(0);              /* not reached */
	} else {
		kill(pid, SIGKILL);
		r = waitpid(pid, &status, 0);
		chk("waitpid3 returns child", r == pid);
		chk("WIFSIGNALED", WIFSIGNALED(status));
		chk("WTERMSIG==SIGKILL", WTERMSIG(status) == SIGKILL);
	}

	if (fails) {
		printf("%d procwait FAIL\n", fails);
		return 1;
	}
	printf("PASS procwait\n");
	return 0;
}
