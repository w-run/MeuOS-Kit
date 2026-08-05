/* startup.c — system entry contract.
 *
 * Called by crt1.S before main() so the C library can capture the
 * process image the kernel handed us: argv[0] for __progname, and the
 * base of the auxiliary vector for getauxval(3).
 * __meuos_startup() is invoked by each arch's crt1.S.  main() arguments
 * are still on the stack untouched at that point, so the function
 * re-derives argv/envp the same way crt1 does, then caches the auxv
 * base before setenv(3) has a chance to reallocate environ (which would
 * otherwise break auxv lookup).
 */

#include <stddef.h>
#include <errno.h>
#include <sys/auxv.h>

extern char **environ;

/* __progname: BSD/glibc convention, the executable name as invoked.
 * crt sets it from argv[0]; defaults to "?" so standalone use (or a crt
 * without startup support) still yields a sane value.  syslog(3),
 * error(3) (compat) and other consumers depend on it. */
const char *__progname = "?";

/* Cached base of the kernel auxv array (pairs of unsigned long). */
static const unsigned long *__auxv_cache;

/* The auxiliary vector immediately follows the NUL-terminated envp
 * array in the initial stack image. */
static const unsigned long *
find_auxv(char **envp)
{
	const char **p;

	if (!envp)
		return NULL;
	for (p = (const char **)envp; *p; ++p)
		/* skip */ ;
	return (const unsigned long *)(p + 1);
}

void
__meuos_startup(int argc, char **argv, char **envp)
{
	(void)argc;
	if (argv && argv[0])
		__progname = argv[0];
	if (!__auxv_cache)
		__auxv_cache = find_auxv(envp);
}

unsigned long
getauxval(unsigned long type)
{
	const unsigned long *p;

	if (!__auxv_cache)
		__auxv_cache = find_auxv(environ);
	for (p = __auxv_cache; p && p[0] != AT_NULL; p += 2) {
		if (p[0] == type)
			return p[1];
	}
	errno = ENOENT;
	return 0;
}
