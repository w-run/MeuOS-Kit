/* stdio/char_io.c -- single-character and line-at-a-time stdio.
 *
 * Provides: getc, fgetc, ungetc, fputc, putc, fputs, putchar, getchar,
 * puts, fgets, fflush, ferror. Each character read goes through getc()
 * which centralises the memory/cookie/fd dispatch plus the ungot slot;
 * writes go through fputc() which mirrors that dispatch. */

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "internal.h"

int
getc(FILE *stream)
{
	unsigned char character;
	ssize_t count;

	if (!stream || !(stream->flags & FILE_READ)) {
		errno = EBADF;
		return EOF;
	}
	if (stream->ungot != EOF) {
		int value = stream->ungot;
		stream->ungot = EOF;
		return value;
	}
	if (stream->flags & FILE_MEMORY) {
		if (stream->pos == stream->size)
			return EOF;
		return stream->memory[stream->pos++];
	}
	if (stream->flags & FILE_COOKIE) {
		if (!stream->readfn)
			return EOF;
		count = stream->readfn(stream->cookie, (char *)&character, 1);
	} else {
		count = read(stream->fd, &character, 1);
	}
	if (count == 1)
		return character;
	if (count < 0)
		stream->flags |= FILE_ERROR;
	return EOF;
}

int
fgetc(FILE *stream)
{
	return getc(stream);
}

int
ungetc(int character, FILE *stream)
{
	if (character == EOF || !stream || !(stream->flags & FILE_READ)
	 || stream->ungot != EOF)
		return EOF;
	stream->ungot = (unsigned char)character;
	return stream->ungot;
}

int
fputc(int character, FILE *stream)
{
	unsigned char byte = (unsigned char)character;

	if (!stream || !(stream->flags & FILE_WRITE) || (stream->flags & FILE_MEMORY)) {
		errno = EBADF;
		return EOF;
	}
	if (stream->flags & FILE_COOKIE) {
		if (!stream->writefn || stream->writefn(stream->cookie,
		    (const char *)&byte, 1) != 1) {
			stream->flags |= FILE_ERROR;
			return EOF;
		}
	} else if (write(stream->fd, &byte, 1) != 1) {
		stream->flags |= FILE_ERROR;
		return EOF;
	}
	return byte;
}

int
putc(int character, FILE *stream)
{
	return fputc(character, stream);
}

int
fputs(const char *text, FILE *stream)
{
	int total = 0;

	while (*text) {
		if (fputc((unsigned char)*text++, stream) == EOF)
			return EOF;
		++total;
	}
	return total;
}

char *
fgets(char *buffer, int size, FILE *stream)
{
	int character;
	int i = 0;

	if (!buffer || size <= 0)
		return NULL;
	while (i + 1 < size && (character = getc(stream)) != EOF) {
		buffer[i++] = (char)character;
		if (character == '\n')
			break;
	}
	if (!i)
		return NULL;
	buffer[i] = 0;
	return buffer;
}

static int
emit(const char *text, size_t count)
{
	return write(1, text, count) == (ssize_t)count ? (int)count : -1;
}

int
putchar(int character)
{
	char text = (char)character;
	return emit(&text, 1) < 0 ? EOF : (unsigned char)text;
}

int
getchar(void)
{
	char character;
	return read(0, &character, 1) == 1 ? (unsigned char)character : EOF;
}

int
puts(const char *text)
{
	size_t length = strlen(text);
	if (emit(text, length) < 0 || emit("\n", 1) < 0)
		return EOF;
	return 0;
}

int
fflush(FILE *stream)
{
	(void)stream;
	return 0;
}

int
ferror(FILE *stream)
{
	return stream && (stream->flags & FILE_ERROR) != 0;
}
