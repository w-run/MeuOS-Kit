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
 *
 * This file also carries __libc_start_main(), the single portable
 * implementation of the startup sequence shared by all six architectures:
 * it runs the .preinit_array/.init_array constructors, calls main(), and
 * hands main's status to exit() (which drains atexit -- including the
 * .fini_array walk registered here -- and flushes stdio).  Keeping the
 * sequence in C means each crt1.S only has to marshal argc/argv/envp and
 * tail-call this function; no per-arch array-walking assembly.
 */

#include <stddef.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/auxv.h>

extern char **environ;

/* __progname: BSD/glibc convention, the executable name as invoked.
 * crt sets it from argv[0]; defaults to "?" so standalone use (or a crt
 * without startup support) still yields a sane value.  syslog(3),
 * error(3) (compat) and other consumers depend on it.
 * __progname_full holds the full argv[0] (glibc also provides this). */
const char *__progname = "?";
const char *__progname_full = "?";

/* GNU/glibc-convention program-invocation globals.  Although glibc exposes
 * these as compat/glibc-isms, they must be filled at libc startup, and this
 * crt has no .init_array for an opt-in compat archive to hook into (nor does
 * the toolchain fold an undefined-weak store reliably — it emits a pcrel
 * load from address 0 -> SIGSEGV; and GAS `.set` aliases are not linkable
 * across archives), so core defines and populates them here.  This mirrors
 * musl, which also carries these argv[0]-driven startup globals in the main
 * libc rather than a GNU-only compat archive.  The compat layer still owns
 * the public declaration header (program-invocation.h) for opt-in programs.
 * core does NO __GLIBC__ masking.  See ARCHITECTURE.md §5 entry contract. */
char *program_invocation_name = "?";
char *program_invocation_short_name = "?";

/* Last pathname component of s; the string is not copied — it aliases
 * inside argv[0]. */
static char *
base_name(char *s)
{
	char *base = s;

	if (!s)
		return NULL;
	for (; *s; ++s) {
		if (*s == '/')
			base = s + 1;
	}
	return base;
}

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
	if (argv && argv[0]) {
		__progname = argv[0];
		__progname_full = argv[0];
		program_invocation_name = argv[0];
		program_invocation_short_name = base_name(argv[0]);
	}
	if (!__auxv_cache)
		__auxv_cache = find_auxv(envp);
}

/* Section-bound symbols for the static-link startup arrays.
 *
 * The linker places every `__attribute__((constructor))` function pointer
 * into .init_array and every `__attribute__((destructor))` one into
 * .fini_array, then synthesises the __{init,fini}_array_{start,end} pairs
 * that delimit them.  Both bounds are declared weak so a program that has
 * no constructors at all -- and therefore no such sections for the linker
 * to bracket -- still links: the bounds then compare equal (both zero) and
 * the walk below is a no-op.
 *
 * The arrays are `void (*)(void)` at the C level.  glibc passes
 * (argc, argv, envp) to .init_array entries; nothing in the Kit relies on
 * that GNU-ism, and the plain form is what the C and C++ standards need
 * for constructors, so we call them with no arguments. */
extern void (*__preinit_array_start[])(void) __attribute__((weak));
extern void (*__preinit_array_end[])(void) __attribute__((weak));
extern void (*__init_array_start[])(void) __attribute__((weak));
extern void (*__init_array_end[])(void) __attribute__((weak));
extern void (*__fini_array_start[])(void) __attribute__((weak));
extern void (*__fini_array_end[])(void) __attribute__((weak));

/* Legacy .init/.fini entry points emitted by crti.o/crtn.o.  Weak because a
 * link that does not pull in the crti/crtn pair simply has neither. */
extern void _init(void) __attribute__((weak));
extern void _fini(void) __attribute__((weak));

static void
run_array(void (**start)(void), void (**end)(void))
{
	size_t i, n;

	if (!start || !end || end <= start)
		return;
	n = (size_t)(end - start);
	for (i = 0; i < n; ++i) {
		if (start[i] && start[i] != (void (*)(void))-1)
			start[i]();
	}
}

/* Destructors run in reverse registration order, the mirror of .init_array. */
static void
run_array_reverse(void (**start)(void), void (**end)(void))
{
	size_t n;

	if (!start || !end || end <= start)
		return;
	n = (size_t)(end - start);
	while (n--) {
		if (start[n] && start[n] != (void (*)(void))-1)
			start[n]();
	}
}

/* Registered with atexit() so destructors fire on the exit() path -- which
 * covers both `return` from main and an explicit exit() from anywhere -- and
 * run before stdio is flushed, so output from a destructor is not lost. */
static void
run_fini(void)
{
	run_array_reverse(__fini_array_start, __fini_array_end);
	if (_fini)
		_fini();
}

/* __libc_csu_init: GNU signature, walks the .preinit_array, calls _init(),
 * then walks the .init_array.  This is the init function passed by the
 * Kit's own crt1.S so that compat archives can rely on having a named
 * entry point to hook into startup.
 *
 * The signature mirrors glibc's __libc_csu_init so gcc/clang programs that
 * link against this libc with their own crt1 (or a foreign one) find the
 * symbol they expect. */
void
__libc_csu_init(int argc, char **argv, char **envp)
{
	(void)argc;
	(void)argv;
	(void)envp;
	run_array(__preinit_array_start, __preinit_array_end);
	if (_init)
		_init();
	run_array(__init_array_start, __init_array_end);
}

/* __libc_csu_fini: GNU signature, walks .fini_array in reverse and calls
 * _fini().  Registered by the Kit's crt1 with __libc_start_main's fini
 * slot so it runs via atexit on the exit() path. */
void
__libc_csu_fini(void)
{
	run_array_reverse(__fini_array_start, __fini_array_end);
	if (_fini)
		_fini();
}

/* The libc startup wrapper.  Each crt1.S computes argc/argv/envp and calls
 * this; it never returns.  `init` and `fini` are the legacy GNU slots
 * (__libc_csu_init / __libc_csu_fini); the Kit's own crt1 now passes the
 * addresses of __libc_csu_init / __libc_csu_fini so that compat archives
 * can reference __libc_csu_init as a startup hook.  A foreign crt1 -- or
 * gcc/clang linking against this libc with its own startup files -- may
 * pass NULL for either; the inline fallback honours that. */
_Noreturn void
__libc_start_main(int (*main_fn)(int, char **, char **), int argc, char **argv,
                  void (*init)(int, char **, char **), void (*fini)(void),
                  void (*rtld_fini)(void), void *stack_end)
{
	char **envp = argv + argc + 1;

	(void)stack_end;

	environ = envp;
	__meuos_startup(argc, argv, envp);

	/* The dynamic loader's own teardown goes on first so it runs last. */
	if (rtld_fini)
		atexit(rtld_fini);
	if (fini)
		atexit(fini);
	else
		atexit(run_fini);

	/* .preinit_array precedes every other constructor by definition.
	 * When init is non-NULL (Kit's own crt1 passes __libc_csu_init) it
	 * handles the whole sequence; when NULL we fall back to the inline
	 * walk so foreign crt1 files still get constructors. */
	if (init)
		init(argc, argv, envp);
	else {
		run_array(__preinit_array_start, __preinit_array_end);
		if (_init)
			_init();
		run_array(__init_array_start, __init_array_end);
	}

	exit(main_fn(argc, argv, envp));
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
