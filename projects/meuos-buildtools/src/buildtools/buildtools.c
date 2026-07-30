/*
 * buildtools.c — BusyBox-style multi-call binary for meuos-buildtools.
 *
 * Invoked via symlink: m4 → buildtools, flex → buildtools, etc.
 * Dispatches to the appropriate tool's _main() based on argv[0].
 *
 * Also supports explicit invocation:
 *   buildtools m4   — runs m4
 *   buildtools flex  — runs flex
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern int m4_main(int argc, char **argv);
extern int gperf_main(void);
extern int flex_main(int argc, char **argv);

static const char *applet_name(const char *argv0)
{
	const char *p = strrchr(argv0, '/');
	return p ? p + 1 : argv0;
}

int main(int argc, char **argv)
{
	const char *name = applet_name(argv[0]);

	/* Explicit subcommand: buildtools m4 ... */
	if (argc > 1 && (strcmp(name, "buildtools") == 0 ||
	                 strcmp(name, "buildtools.exe") == 0)) {
		name = argv[1];
		argv++;
		argc--;
	}

	if (strcmp(name, "m4") == 0)
		return m4_main(argc, argv);
	if (strcmp(name, "gperf") == 0)
		return gperf_main();
	if (strcmp(name, "flex") == 0)
		return flex_main(argc, argv);

	fprintf(stderr, "buildtools: unknown applet '%s'\n", name);
	fprintf(stderr, "Usage: ln -s buildtools <toolname>\n");
	fprintf(stderr, "  or:  buildtools <toolname> [args...]\n");
	fprintf(stderr, "Tools: m4, gperf, flex\n");
	return 1;
}
