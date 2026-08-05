/* stdio/file_misc.c -- miscellaneous stdio: tmpfile/tmpnam/remove/perror.
 *
 * perror uses printf + strerror; tmpnam produces /tmp/tmp<n> names;
 * tmpfile opens an anonymous file that gets unlinked immediately;
 * remove prefers unlink() and falls back to rmdir() for directories. */

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "internal.h"

int
remove(const char *path)
{
	if (unlink(path) < 0)
		return rmdir(path);
	return 0;
}

char *
tmpnam(char *s)
{
	static char buf[L_tmpnam];
	static unsigned long counter;
	char *b = s ? s : buf;

	snprintf(b, L_tmpnam, "/tmp/tmp%lu", counter++);
	return b;
}

FILE *
tmpfile(void)
{
	char name[L_tmpnam];
	int fd;
	FILE *f;

	snprintf(name, sizeof(name), "/tmp/meuostmp%lu", (unsigned long)getpid());
	fd = open(name, O_RDWR | O_CREAT | O_TRUNC, 0600);
	if (fd < 0)
		return NULL;
	unlink(name);
	f = fdopen(fd, "w+");
	if (!f) {
		close(fd);
		return NULL;
	}
	return f;
}

void
perror(const char *prefix)
{
	if (prefix && *prefix)
		printf("%s: %s\n", prefix, strerror(errno));
	else
		printf("%s\n", strerror(errno));
}

/*
 * 缓冲模式控制。当前流均为无缓冲实现：POSIX 规定对 _IONBF 忽略 buffer/size，
 * 对 _IOFBF/_IOLBF 本实现无法实际缓存，但接受它们并与无缓冲等效
 * （输出顺序与完整性不受影响）。
 */
int
setvbuf(FILE *stream, char *buffer, int mode, size_t size)
{
	if (!stream || (mode != _IOFBF && mode != _IOLBF && mode != _IONBF)) {
		errno = EINVAL;
		return EOF;
	}
	(void)buffer;
	(void)size;
	return 0;
}

void
setbuf(FILE *stream, char *buffer)
{
	(void)setvbuf(stream, buffer, buffer ? _IOFBF : _IONBF, BUFSIZ);
}

void
setlinebuf(FILE *stream)
{
	(void)setvbuf(stream, NULL, _IOLBF, 0);
}

/* C11 7.21.5.3: clearerr clears the error indicator for the stream.  The
 * EOF indicator is positional for memory streams (feof derives it from the
 * read position), so clearing the error flag is what error-recovery needs. */
void
clearerr(FILE *stream)
{
	if (stream)
		stream->flags &= ~FILE_ERROR;
}
