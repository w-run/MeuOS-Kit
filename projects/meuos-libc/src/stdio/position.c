/* stdio/position.c -- file position and status queries.
 *
 * fseek/ftell/rewind cover SEEK_SET/CUR/END for fd-backed streams and
 * pure pointer arithmetic for fmemopen streams. Cookie streams return
 * ESPIPE because we don't currently plumb a seek callback through
 * fopencookie. feof is approximated for memory streams; for fd/cookie
 * streams we cannot know without a real read. */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>
#include "internal.h"

int
fseek(FILE *stream, long offset, int whence)
{
	if (!stream) {
		errno = EBADF;
		return -1;
	}
	if (stream->flags & FILE_MEMORY) {
		switch (whence) {
		case SEEK_SET: stream->pos = offset; break;
		case SEEK_CUR: stream->pos += offset; break;
		case SEEK_END: stream->pos = stream->size + offset; break;
		default: errno = EINVAL; return -1;
		}
		stream->ungot = EOF;
		return 0;
	}
	if (stream->flags & FILE_COOKIE) {
		/* cookie seek not supported in minimal implementation */
		errno = ESPIPE;
		return -1;
	}
	{
		off_t r = lseek(stream->fd, offset, whence);

		if (r < 0)
			return -1;
		stream->ungot = EOF;
	}
	return 0;
}

long
ftell(FILE *stream)
{
	if (!stream) {
		errno = EBADF;
		return -1;
	}
	if (stream->flags & FILE_MEMORY)
		return (long)stream->pos;
	if (stream->flags & FILE_COOKIE) {
		errno = ESPIPE;
		return -1;
	}
	{
		off_t pos = lseek(stream->fd, 0, SEEK_CUR);

		if (stream->ungot != EOF && pos > 0)
			--pos;
		return (long)pos;
	}
}

void
rewind(FILE *stream)
{
	fseek(stream, 0L, SEEK_SET);
	stream->flags &= ~FILE_ERROR;
}

int
feof(FILE *stream)
{
	if (!stream)
		return 0;
	if (stream->flags & FILE_MEMORY)
		return stream->pos >= stream->size;
	/* For fd/cookie streams we can't know without trying to read. */
	return 0;
}

int
fgetpos(FILE *stream, fpos_t *pos)
{
	long p = ftell(stream);

	if (p < 0)
		return -1;
	*pos = (fpos_t)p;
	return 0;
}

int
fsetpos(FILE *stream, const fpos_t *pos)
{
	return fseek(stream, (long)*pos, SEEK_SET);
}

int
fileno(FILE *stream)
{
	if (!stream)
		return -1;
	return stream->fd;
}
