/* sync_gate.c — sync/fsync/fdatasync regression gate.
 * Opens a scratch file, writes to it, then syncs it both ways and checks
 * the syscalls report success; also checks an invalid fd is rejected with
 * EBADF. sync() is void but executes the underlying writeback syscall. */
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* sys/resource.h / unistd.h expose fsync/fdatasync/sync. */

static int fails;

static void
chk(const char *lbl, int rc, int want)
{
	if (rc != want) {
		printf("FAIL: %s rc=%d errno=%d (%s)\n", lbl, rc, errno, strerror(errno));
		fails++;
	}
}

int
main(void)
{
	const char *path = "/tmp/meuos-sync-gate.tmp";
	char buf[] = "flushed sync gate payload\n";
	int fd;

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) {
		printf("FAIL: open errno=%d (%s)\n", errno, strerror(errno));
		return 1;
	}

	if (write(fd, buf, sizeof buf - 1) != (ssize_t)(sizeof buf - 1)) {
		printf("FAIL: write\n");
		fails++;
	}

	/* fsync on a real file: should flush and return 0 */
	chk("fsync", fsync(fd), 0);

	/* fdatasync: should also flush data and return 0 */
	chk("fdatasync", fdatasync(fd), 0);

	/* sync() has no return value; merely executing the syscall without
	 * crashing is the assertion here. */
	sync();

	close(fd);

	/* fsync on an invalid fd: EBADF */
	errno = 0;
	chk("fsync badfd", fsync(-1), -1);
	if (errno != EBADF) {
		printf("FAIL: fsync(-1) errno=%d want EBADF\n", errno);
		fails++;
	}

	/* fdatasync on an invalid fd: EBADF */
	errno = 0;
	chk("fdatasync badfd", fdatasync(-1), -1);
	if (errno != EBADF) {
		printf("FAIL: fdatasync(-1) errno=%d want EBADF\n", errno);
		fails++;
	}

	unlink(path);

	if (fails) {
		printf("%d sync FAIL\n", fails);
		return 1;
	}
	printf("PASS sync\n");
	return 0;
}
