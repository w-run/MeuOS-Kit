#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stddef.h>

/* getline / getdelim -- POSIX.1-2008, but historically a glibc extension.
 * Implemented on top of fgetc/realloc; no FILE internals needed. */
ssize_t
getdelim(char **lineptr, size_t *n, int delimiter, FILE *stream)
{
	char *buf = *lineptr;
	size_t size = *n;
	size_t len = 0;
	int c;

	if (!lineptr || !n || !stream) {
		errno = EINVAL;
		return -1;
	}
	if (!buf || size == 0) {
		size = 128;
		buf = malloc(size);
		if (!buf)
			return -1;
	}
	while ((c = fgetc(stream)) != EOF) {
		if (len + 2 > size) {
			size_t newsize = size * 2;
			char *newbuf = realloc(buf, newsize);

			if (!newbuf) {
				if (buf != *lineptr)
					free(buf);
				return -1;
			}
			buf = newbuf;
			size = newsize;
		}
		buf[len++] = (char)c;
		if ((unsigned char)c == (unsigned char)delimiter)
			break;
	}
	if (len == 0 && c == EOF)
		return -1;
	buf[len] = '\0';
	*lineptr = buf;
	*n = size;
	return (ssize_t)len;
}

ssize_t
getline(char **lineptr, size_t *n, FILE *stream)
{
	return getdelim(lineptr, n, '\n', stream);
}
