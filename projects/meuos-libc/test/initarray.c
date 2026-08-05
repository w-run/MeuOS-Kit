/* initarray.c — startup contract gate.
 *
 * Proves the .init_array / .fini_array path that __libc_start_main() walks:
 *   - .init_array entries run before main(), in array order;
 *   - .fini_array entries run after main(), in reverse order;
 *   - constructors observe a live libc (stdio, malloc) and the process
 *     image captured by __libc_start_main (__progname, environ);
 *   - destructors still run when the program leaves via exit() rather than
 *     by returning from main, and their stdio output is flushed.
 *
 * The array entries themselves come from the companion initarray_arr.S.
 * They are not written as __attribute__((constructor)) here because mcc
 * currently parses that attribute but emits no .init_array entry for it
 * (see .todo/mcc/init-array-attribute.md); once mcc grows that support the
 * .S file can be dropped and the attributes used directly.  What is under
 * test on this side is the libc runtime contract, which is the same either
 * way: the linker hands us the arrays and __libc_start_main walks them.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern char **environ;
extern const char *__progname;

static char trace[64];
static int  main_ran;

static void
note(const char *tag)
{
	if (strlen(trace) + strlen(tag) + 1 < sizeof trace)
		strcat(trace, tag);
}

/* Referenced from initarray_arr.S; must have external linkage. */
void
ctor_first(void)
{
	note("C1");
	/* libc must already be usable this early. */
	if (!environ) {
		fputs("initarray: environ not set in constructor\n", stderr);
		_Exit(1);
	}
	if (!__progname || !*__progname) {
		fputs("initarray: __progname not set in constructor\n", stderr);
		_Exit(1);
	}
	if (main_ran) {
		fputs("initarray: constructor ran after main\n", stderr);
		_Exit(1);
	}
}

void
ctor_second(void)
{
	void *p = malloc(32);	/* malloc must work this early too */

	if (!p) {
		fputs("initarray: malloc failed in constructor\n", stderr);
		_Exit(1);
	}
	free(p);
	note("C2");
}

/* .fini_array is walked in reverse, so dtor_second runs before dtor_first. */
void
dtor_second(void)
{
	note("D2");
}

void
dtor_first(void)
{
	note("D1");
	if (strcmp(trace, "C1C2MD2D1") != 0) {
		fprintf(stderr, "initarray: bad order %s (want C1C2MD2D1)\n",
		        trace);
		_Exit(1);
	}
	/* This must reach the terminal: exit() flushes stdio only after the
	 * atexit chain -- which is what runs the destructors -- has drained. */
	puts("initarray ok");
}

int
main(void)
{
	main_ran = 1;
	note("M");
	if (strcmp(trace, "C1C2M") != 0) {
		fprintf(stderr, "initarray: constructors did not all run "
		                "before main (%s)\n", trace);
		return 1;
	}
	/* Leave via exit() rather than return so the gate also covers the
	 * explicit-exit path. */
	exit(0);
}
