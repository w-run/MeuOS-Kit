/* spawn_gate.c — posix_spawn regression gate.
 * Spawns /bin/sh -c "echo SPAWNED_OK" with a file action redirecting the
 * child's stdout to a temp file, then reconstructs the message from a file
 * descriptor; waits for the child and asserts it produced the expected
 * output.  Exercises posix_spawn + file_actions (addopen/dup2/destroy). */
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>

int
main(void)
{
	char outfile[64];
	strcpy(outfile, "/tmp/spawn_gate.XXXXXX");
	int wfd = mkstemp(outfile);
	if (wfd < 0) { perror("mkstemp"); return 1; }
	/* re-open wfd at fixed fd 5 so we can dup2 it later */
	if (dup2(wfd, 5) < 0) return 1;
	close(wfd);

	char *argv[] = { (char *)"/bin/sh", (char *)"-c",
	                 (char *)"echo SPAWNED_OK", NULL };
	char *envp[] = { NULL };

	posix_spawn_file_actions_t fa;
	posix_spawnattr_t attr;
	if (posix_spawn_file_actions_init(&fa) != 0) return 1;
	if (posix_spawn_file_actions_adddup2(&fa, 5, 1) != 0) return 1; /* stdout->5 */
	posix_spawnattr_init(&attr);

	pid_t pid;
	int ret = posix_spawn(&pid, "/bin/sh", &fa, &attr, argv, envp);
	if (ret != 0) {
		printf("FAIL: posix_spawn = %d (%s)\n", ret, strerror(ret));
		return 1;
	}

	int status;
	waitpid(pid, &status, 0);
	posix_spawn_file_actions_destroy(&fa);
	posix_spawnattr_destroy(&attr);

	/* child stdout was dup2'd to fd 5 == outfile */
	int rfd = open(outfile, O_RDONLY);
	if (rfd < 0) { perror("open outfile"); return 1; }
	char buf[64];
	ssize_t n = read(rfd, buf, sizeof buf - 1);
	close(rfd); unlink(outfile);
	if (n < 0) { perror("read"); return 1; }
	buf[n] = 0;

	if (strstr(buf, "SPAWNED_OK") == NULL) {
		printf("FAIL: output '%s' missing SPAWNED_OK (status=%d)\n", buf, status);
		return 1;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		printf("FAIL: child status=%d (want exited 0)\n", status);
		return 1;
	}

	printf("PASS spawn\n");
	return 0;
}
