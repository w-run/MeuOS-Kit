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
