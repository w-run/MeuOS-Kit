#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <unistd.h>
#include <stdio.h>

#define ATEXIT_MAX 32

static void (*handlers[ATEXIT_MAX])(void);
static unsigned int handler_count;

int
atexit(void (*handler)(void))
{
	if (!handler || handler_count == ATEXIT_MAX)
		return -1;
	handlers[handler_count++] = handler;
	return 0;
}

_Noreturn void
exit(int status)
{
	while (handler_count)
		handlers[--handler_count]();
	/* Flush all stdio streams before the raw _exit syscall: crt1.S
	 * calls exit() rather than syscall-ing directly so that buffered
	 * printf output reaches the file/tty even when stdout is not a
	 * line-buffered tty (e.g. redirected to a 9p file in the qvm
	 * aarch64 runtime tests). */
	fflush(NULL);
	_exit(status);
}

_Noreturn void
abort(void)
{
	_exit(134);
}

char *
mktemp(char *template)
{
	static unsigned long counter;
	char *p = template;
	size_t len = 0;

	while (p[len])
		++len;
	for (int i = 0; i < 6 && len > 0; ++i)
		p[--len] = 'A' + (counter++ % 26);
	return template;
}

char *
canonicalize_file_name(const char *path)
{
	return realpath(path, NULL);
}

char *
realpath(const char *path, char *resolved)
{
	/* Minimal realpath: just copy the path.  A proper implementation would
	 * resolve symlinks, '.', '..', etc. but many programs work with the
	 * simple form.
	 *
	 * We always compute into our own buffer first and then copy only the
	 * actual result length into the caller's buffer, so a short resolved
	 * buffer is never clobbered by a fixed 4096-byte block write.  POSIX
	 * still requires the caller's buffer to be at least PATH_MAX bytes
	 * (4096) when resolved is non-NULL. */
	char buf[4096];
	ssize_t n;
	size_t len;

	if (!path) {
		errno = EINVAL;
		return NULL;
	}
	if (strlen(path) >= sizeof(buf)) {
		errno = ENAMETOOLONG;
		return NULL;
	}
	/* Try readlink first in case it's a symlink */
	n = readlink(path, buf, sizeof(buf) - 1);
	if (n > 0) {
		buf[n] = '\0';
	} else {
		strcpy(buf, path);
	}
	len = strlen(buf) + 1;
	if (resolved) {
		memcpy(resolved, buf, len);
		return resolved;
	}
	return strdup(buf);
}
