/* Vector/positioned I/O + *at/pipe2/dup3 regression gate (P1 补齐). */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

static const char payload[] = "0123456789abcdef";

int
main(void)
{
	char tmpl[] = "/tmp/meuos-uio-XXXXXX";
	char buf[64];
	struct iovec iov[2];
	int fd, p[2];
	long fsz;

	fd = mkstemp(tmpl);
	if (fd < 0)
		return 10;

	/* pwrite + pread at offset. */
	if (pwrite(fd, payload, 16, 0) != 16)
		return 11;
	if (pread(fd, buf, 8, 0) != 8 || memcmp(buf, "01234567", 8) != 0)
		return 12;
	if (pread(fd, buf, 8, 8) != 8 || memcmp(buf, "89abcdef", 8) != 0)
		return 13;

	/* writev + readv (sequential, from a known offset). */
	if (lseek(fd, 0, SEEK_SET) != 0)
		return 19;
	iov[0].iov_base = (void *)"AB"; iov[0].iov_len = 2;
	iov[1].iov_base = (void *)"cd"; iov[1].iov_len = 2;
	if (writev(fd, iov, 2) != 4)
		return 14;
	if (lseek(fd, 0, SEEK_SET) != 0)
		return 18;
	memset(buf, 0, sizeof buf);
	iov[0].iov_base = buf;      iov[0].iov_len = 3;
	iov[1].iov_base = buf + 3;  iov[1].iov_len = 1;
	if (readv(fd, iov, 2) != 4 || memcmp(buf, "ABcd", 4) != 0)
		return 15;

	/* pwritev + preadv at offset 16. */
	iov[0].iov_base = (void *)"XY"; iov[0].iov_len = 2;
	iov[1].iov_base = (void *)"ZW"; iov[1].iov_len = 2;
	if (pwritev(fd, iov, 2, 16) != 4)
		return 16;
	memset(buf, 0, sizeof buf);
	iov[0].iov_base = buf;      iov[0].iov_len = 2;
	iov[1].iov_base = buf + 2;  iov[1].iov_len = 2;
	if (preadv(fd, iov, 2, 16) != 4 || memcmp(buf, "XYZW", 4) != 0)
		return 17;

	/* openat with AT_FDCWD == open. */
	close(fd);
	fd = openat(AT_FDCWD, tmpl, O_RDONLY);
	if (fd < 0)
		return 18;
	if (read(fd, buf, 4) != 4 || memcmp(buf, "ABcd", 4) != 0)
		return 19;
	close(fd);

	/* pipe2 + dup3. */
	if (pipe2(p, O_CLOEXEC) != 0)
		return 20;
	if (write(p[1], "go", 2) != 2)
		return 21;
	{
		int q = dup3(p[0], 9, 0);
		if (q != 9)
			return 22;
		if (read(q, buf, 2) != 2 || memcmp(buf, "go", 2) != 0)
			return 23;
		close(q);
	}
	close(p[0]); close(p[1]);

	fsz = lseek(fd, 0, SEEK_END);
	unlink(tmpl);
	(void)fsz;
	puts("PASS uio");
	return 0;
}
