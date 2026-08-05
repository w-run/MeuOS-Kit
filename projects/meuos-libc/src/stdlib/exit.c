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

/* C99 7.22.4.4.  Unlike exit(), no atexit handler runs and no stream is
 * flushed -- control goes straight to the kernel.  Constructors and other
 * code that must bail out without re-entering the termination chain (which
 * would recurse, since the .fini_array walk is itself an atexit handler)
 * use this. */
_Noreturn void
_Exit(int status)
{
	_exit(status);
}

/* quick_exit keeps a handler list of its own (C11 7.22.4.3): at_quick_exit
 * registrations are not atexit registrations and vice versa. */
static void (*quick_handlers[ATEXIT_MAX])(void);
static unsigned int quick_handler_count;

int
at_quick_exit(void (*handler)(void))
{
	if (!handler || quick_handler_count == ATEXIT_MAX)
		return -1;
	quick_handlers[quick_handler_count++] = handler;
	return 0;
}

/* C11 7.22.4.7: run the at_quick_exit handlers in reverse registration
 * order, then terminate.  No atexit handler runs and nothing is flushed. */
_Noreturn void
quick_exit(int status)
{
	while (quick_handler_count)
		quick_handlers[--quick_handler_count]();
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
