/* stdio/block_io.c -- array/buffered stdio: fread and fwrite.
 *
 * Both loop one byte at a time through fputc()/getc() so they share the
 * same cookie/memory/fd dispatch. This is the same approach musl and
 * cproc take; we keep it because block_io is exercised by bzip2 and
 * binutils and they need correct EOF handling more than throughput. */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include "internal.h"

size_t
fread(void *ptr, size_t size, size_t count, FILE *stream)
{
	unsigned char *p = ptr;
	size_t total = size * count;
	size_t done = 0;

	if (!stream || !(stream->flags & FILE_READ)) {
		errno = EBADF;
		return 0;
	}
	if (size == 0)
		return 0;	/* 避免返回 done / size 时 0/0 除零 */
	while (done < total) {
		int c = getc(stream);

		if (c == EOF)
			break;
		p[done++] = (unsigned char)c;
	}
	return done / size;
}

size_t
fwrite(const void *ptr, size_t size, size_t count, FILE *stream)
{
	const unsigned char *p = ptr;
	size_t total = size * count;
	size_t i;

	if (!stream || !(stream->flags & FILE_WRITE) || (stream->flags & FILE_MEMORY)) {
		errno = EBADF;
		return 0;
	}
	if (size == 0)
		return 0;
	for (i = 0; i < total; ++i) {
		if (fputc(p[i], stream) == EOF)
			return i / size;	/* fwrite 返回元素数而非字节数 */
	}
	return count;
}
