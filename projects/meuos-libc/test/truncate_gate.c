/* truncate_gate.c — truncate()/ftruncate() regression gate.
 *
 * Verifies ftruncate shrinks an open file and truncate() shrinks by path,
 * that the new sizes are observable via fstat/st_size, and that invalid
 * descriptors/paths fail with the expected errno. */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
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

static void
fill_file(int fd, int nbytes)
{
	char buf[64];
	memset(buf, 'x', sizeof buf);
	int left = nbytes;
	while (left > 0) {
		int w = (left > (int)sizeof buf) ? (int)sizeof buf : left;
		write(fd, buf, (size_t)w);
		left -= w;
	}
}

int
main(void)
{
	const char *path = "/tmp/meuos-truncate-gate.tmp";
	struct stat st;
	int fd;

	/* build a 256-byte file */
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) { perror("open"); return 1; }
	fill_file(fd, 256);
	close(fd);

	/* ftruncate to 32 bytes */
	fd = open(path, O_WRONLY);
	if (fd < 0) { perror("open2"); return 1; }
	errno = 0;
	chk("ftruncate fd", ftruncate(fd, 32) == 0);
	if (fstat(fd, &st) != 0) { perror("fstat"); fails++; }
	else chk("ftruncate size==32", st.st_size == 32);
	close(fd);

	/* truncate(path) to 0 */
	errno = 0;
	chk("truncate path", truncate(path, 0) == 0);
	fd = open(path, O_RDONLY);
	if (fd < 0) { perror("open3"); return 1; }
	if (fstat(fd, &st) != 0) { perror("fstat2"); fails++; }
	else chk("truncate size==0", st.st_size == 0);
	close(fd);

	/* grow via truncate (sparse) */
	errno = 0;
	chk("truncate grow", truncate(path, 1000) == 0);
	fd = open(path, O_RDONLY);
	if (fstat(fd, &st) == 0) chk("truncate size==1000", st.st_size == 1000);
	close(fd);

	unlink(path);

	/* error paths */
	errno = 0;
	if (ftruncate(-1, 10) != -1) {
		printf("FAIL: ftruncate(-1) should fail\n"); fails++;
	} else if (errno != EBADF) {
		printf("FAIL: ftruncate(-1) errno=%d want EBADF\n", errno); fails++;
	}
	errno = 0;
	if (truncate("/nonexistent/meuos-xyz", 10) != -1) {
		printf("FAIL: truncate(badpath) should fail\n"); fails++;
	} else if (errno != ENOENT) {
		printf("FAIL: truncate(badpath) errno=%d want ENOENT\n", errno); fails++;
	}

	if (fails) {
		printf("%d truncate FAIL\n", fails);
		return 1;
	}
	printf("PASS truncate\n");
	return 0;
}
