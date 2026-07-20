/* stdio/file.c -- stdio opening, closing and stream allocation.
 *
 * Owns the __meuos_FILE struct definition (matching stdio/internal.h),
 * the stream_mode helper, and the global stdin/stdout/stderr streams.
 * fopen / fdopen / freopen / fclose / fmemopen live here; the data
 * movement primitives are in char_io.c, block_io.c, position.c. */

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "internal.h"

struct __meuos_FILE __meuos_stdin  = { 0, FILE_READ  | FILE_STATIC, 0, 0, 0, EOF };
struct __meuos_FILE __meuos_stdout = { 1, FILE_WRITE | FILE_STATIC, 0, 0, 0, EOF };
struct __meuos_FILE __meuos_stderr = { 2, FILE_WRITE | FILE_STATIC, 0, 0, 0, EOF };
FILE *stdin  = &__meuos_stdin;
FILE *stdout = &__meuos_stdout;
FILE *stderr = &__meuos_stderr;

int
__meuos_stream_mode(const char *mode, int *open_flags, unsigned *stream_flags)
{
	int plus = 0;

	if (!mode || !*mode)
		return -1;
	if (mode[1] == '+')
		plus = 1;
	switch (mode[0]) {
	case 'r':
		*open_flags = plus ? O_RDWR : O_RDONLY;
		*stream_flags = FILE_READ | (plus ? FILE_WRITE : 0);
		return 0;
	case 'w':
		*open_flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
		*stream_flags = FILE_WRITE | (plus ? FILE_READ : 0);
		return 0;
	case 'a':
		*open_flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
		*stream_flags = FILE_WRITE | (plus ? FILE_READ : 0);
		return 0;
	default:
		return -1;
	}
}

FILE *
fdopen(int fd, const char *mode)
{
	struct __meuos_FILE *stream;
	int open_flags;
	unsigned stream_flags;

	if (fd < 0 || __meuos_stream_mode(mode, &open_flags, &stream_flags) < 0) {
		errno = EINVAL;
		return NULL;
	}
	stream = malloc(sizeof(*stream));
	if (!stream)
		return NULL;
	stream->fd = fd;
	stream->flags = stream_flags;
	stream->memory = NULL;
	stream->size = 0;
	stream->pos = 0;
	stream->ungot = EOF;
	return stream;
}

FILE *
fopen(const char *path, const char *mode)
{
	int flags;
	unsigned stream_flags;
	int fd;

	if (__meuos_stream_mode(mode, &flags, &stream_flags) < 0) {
		errno = EINVAL;
		return NULL;
	}
	fd = open(path, flags, 0666);
	if (fd < 0)
		return NULL;
	return fdopen(fd, mode);
}

FILE *
fmemopen(void *buffer, size_t size, const char *mode)
{
	struct __meuos_FILE *stream;
	int ignored;
	unsigned flags;

	if (!buffer || __meuos_stream_mode(mode, &ignored, &flags) < 0 || !(flags & FILE_READ)) {
		errno = EINVAL;
		return NULL;
	}
	stream = malloc(sizeof(*stream));
	if (!stream)
		return NULL;
	stream->fd = -1;
	stream->flags = flags | FILE_MEMORY;
	stream->memory = buffer;
	stream->size = size;
	stream->pos = 0;
	stream->ungot = EOF;
	return stream;
}

int
fclose(FILE *stream)
{
	if (!stream) {
		errno = EINVAL;
		return EOF;
	}
	if (stream->flags & FILE_COOKIE) {
		if (stream->closefn && stream->closefn(stream->cookie) < 0) {
			stream->flags |= FILE_ERROR;
			return EOF;
		}
	} else if (!(stream->flags & FILE_MEMORY) && !(stream->flags & FILE_STATIC)
		 && close(stream->fd) < 0) {
		stream->flags |= FILE_ERROR;
		return EOF;
	}
	if (!(stream->flags & FILE_STATIC))
		free(stream);
	return 0;
}

FILE *
freopen(const char *path, const char *mode, FILE *stream)
{
	int flags;
	unsigned stream_flags;
	int fd;

	if (!stream || __meuos_stream_mode(mode, &flags, &stream_flags) < 0) {
		errno = EINVAL;
		return NULL;
	}
	fd = open(path, flags, 0666);
	if (fd < 0)
		return NULL;
	if (!(stream->flags & FILE_MEMORY))
		(void)close(stream->fd);
	stream->fd = fd;
	stream->flags = stream_flags | (stream->flags & FILE_STATIC);
	stream->memory = NULL;
	stream->size = 0;
	stream->pos = 0;
	stream->ungot = EOF;
	return stream;
}
