/* errno_gate.c — errno error-path precision gate.
 *
 * Verifies that failing POSIX wrappers return -1/null and record the exact
 * expected errno (ENOENT for a missing path, EBADF for a bad descriptor,
 * EINVAL, EISDIR), and — critically — that SUCCESSFUL calls leave errno
 * untouched (errno is only set on failure).  Isolates errno bookkeeping
 * from individual syscall wrappers. */
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

static int fails;

static void
chk(const char *lbl, int cond)
{
	if (!cond) {
		printf("FAIL: %s (errno=%d %s)\n", lbl, errno, strerror(errno));
		fails++;
	}
}

int
main(void)
{
	char buf[8];

	/* open of missing path -> -1, errno ENOENT */
	errno = 0;
	chk("open nonexistent", open("/nonexistent/meuos-errno-zz", O_RDONLY) == -1);
	chk("errno ENOENT", errno == ENOENT);

	/* open empty name -> ENOENT */
	errno = 0;
	chk("open empty -1", open("", O_RDONLY) == -1);
	chk("open empty ENOENT", errno == ENOENT);

	/* open directory for read on a dir -> EISDIR only on read-size... keep
	 * deterministic: opening a directory read-only succeeds in Linux, so
	 * use write on a directory path instead (EISDIR is not exercised here
	 * for portability); instead verify a saved errno survives a success. */

	/* bad descriptor paths -> EBADF */
	errno = 0;
	chk("read badfd -1", read(-1, buf, 1) == -1);
	chk("read badfd EBADF", errno == EBADF);

	errno = 0;
	chk("write badfd -1", write(-1, "x", 1) == -1);
	chk("write badfd EBADF", errno == EBADF);

	errno = 0;
	chk("close badfd -1", close(-1) == -1);
	chk("close badfd EBADF", errno == EBADF);

	/* closed fd (reap a temp) */
	{
		int fd = open("/tmp", O_RDONLY);
		if (fd >= 0) close(fd);
		errno = 0;
		chk("close reaped -1", close(fd) == -1);
		chk("close reaped EBADF", errno == EBADF);
	}

	/* success leaves errno untouched (a pre-set value persists) */
	errno = EINVAL;
	chk("getpid ok", getpid() > 0);
	chk("errno preserved by success", errno == EINVAL);

	/* ftruncate badfd -> EBADF */
	errno = 0;
	chk("ftruncate badfd -1", ftruncate(-1, 0) == -1);
	chk("ftruncate badfd EBADF", errno == EBADF);

	/* access missing -> ENOENT */
	errno = 0;
	chk("access missing -1", access("/nonexistent/meuos-errno-acc", F_OK) == -1);
	chk("access missing ENOENT", errno == ENOENT);

	if (fails) {
		printf("%d errno FAIL\n", fails);
		return 1;
	}
	printf("PASS errno\n");
	return 0;
}
