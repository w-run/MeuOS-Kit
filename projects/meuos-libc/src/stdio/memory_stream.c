/* stdio/memory_stream.c -- fopencookie (glibc) and funopen (BSD).
 *
 * Both create a FILE whose I/O is dispatched to caller-provided callbacks
 * instead of an fd. fopencookie uses ssize_t-based callbacks; funopen's
 * BSD legacy uses int-based callbacks, so we adapt them through a small
 * adapter cookie. Implemented here (not in compat/) because it needs
 * access to the internal FILE struct layout. */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include "internal.h"

FILE *
fopencookie(void *cookie, const char *mode, cookie_io_functions_t funcs)
{
	struct __meuos_FILE *stream;
	int ignored;
	unsigned flags;

	if (__meuos_stream_mode(mode, &ignored, &flags) < 0) {
		errno = EINVAL;
		return NULL;
	}
	stream = malloc(sizeof(*stream));
	if (!stream)
		return NULL;
	stream->fd = -1;
	stream->flags = flags | FILE_COOKIE;
	stream->memory = NULL;
	stream->size = 0;
	stream->pos = 0;
	stream->ungot = EOF;
	stream->cookie = cookie;
	stream->readfn = funcs.read;
	stream->writefn = funcs.write;
	stream->seekfn = NULL;
	stream->closefn = funcs.close;
	return stream;
}

struct __meuos_funopen_cookie {
	void *cookie;
	int (*readfn)(void *, char *, int);
	int (*writefn)(void *, const char *, int);
	long (*seekfn)(void *, long, int);
	int (*closefn)(void *);
};

static ssize_t
funopen_read(void *c, char *buf, size_t n)
{
	struct __meuos_funopen_cookie *fc = c;

	return fc->readfn ? (ssize_t)fc->readfn(fc->cookie, buf, (int)n) : -1;
}

static ssize_t
funopen_write(void *c, const char *buf, size_t n)
{
	struct __meuos_funopen_cookie *fc = c;

	return fc->writefn ? (ssize_t)fc->writefn(fc->cookie, buf, (int)n) : -1;
}

static int
funopen_close(void *c)
{
	struct __meuos_funopen_cookie *fc = c;
	int r = fc->closefn ? fc->closefn(fc->cookie) : 0;

	free(fc);
	return r;
}

FILE *
funopen(const void *cookie,
    int (*readfn)(void *, char *, int),
    int (*writefn)(void *, const char *, int),
    long (*seekfn)(void *, long, int),
    int (*closefn)(void *))
{
	struct __meuos_funopen_cookie *fc;
	cookie_io_functions_t funcs;

	fc = malloc(sizeof(*fc));
	if (!fc)
		return NULL;
	fc->cookie = (void *)cookie;
	fc->readfn = readfn;
	fc->writefn = writefn;
	fc->seekfn = seekfn;
	fc->closefn = closefn;
	funcs.read = funopen_read;
	funcs.write = funopen_write;
	funcs.seek = NULL;
	funcs.close = funopen_close;
	return fopencookie(fc, readfn ? "r" : "w", funcs);
}
