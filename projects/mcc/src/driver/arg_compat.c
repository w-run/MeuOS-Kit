/* arg_compat.c - gcc/clang command-line compatibility shim.
 *
 * Per project convention, multi-letter options use double-dash
 * (--static, --shared, ...). For gcc/clang compatibility, the
 * traditional single-dash forms (-static, -shared, ...) are also
 * accepted and normalized to double-dash before main parsing.
 *
 * Single-letter short options (-c, -o, -S, -E, -D, -I, -O, ...) stay
 * single-dash; they follow POSIX convention.
 *
 * Category-prefixed options (-W<warning>, -f<feature>, -m<machine>)
 * stay single-dash: the first letter is a category marker and the
 * rest is a name, not a "multi-letter option".
 *
 * This file is the ONLY place that knows about gcc compatibility.
 * The main parser in main.c only handles --double-dash multi-letter
 * options and -single-letter short options.
 *
 * Normalization allocates a new argv array; the original argv is
 * not modified. mcc is short-lived so we don't bother freeing the
 * substituted strings (leak on exit is harmless). */
#include <stdlib.h>
#include <string.h>

/* Multi-letter options with exact-match single-dash gcc form.
 * No attached =value. */
static const struct {
	const char *short_form;
	const char *long_form;
} exact_map[] = {
	{"-static",           "--static"},
	{"-shared",           "--shared"},
	{"-nostdinc",         "--nostdinc"},
	{"-nostdlib",         "--nostdlib"},
	{"-nodefaultlibs",    "--nodefaultlibs"},
	{"-pedantic",         "--pedantic"},
	{"-pedantic-errors", "--pedantic-errors"},
	{"-pipe",             "--pipe"},
	{"-pie",              "--pie"},
	{NULL, NULL}
};

/* Bare multi-letter options that take a separated argument.
 * (-target foo -> --target foo; -specs foo -> --specs foo) */
static const struct {
	const char *short_form;
	const char *long_form;
} bare_arg_map[] = {
	{"-target", "--target"},
	{"-specs",  "--specs"},
	{NULL, NULL}
};

/* Prefix-based options: -X=value -> --X=value.
 * Only for multi-letter X (not single-letter category prefixes). */
static const struct {
	const char *prefix;      /* e.g. "-specs=" */
	size_t len;              /* strlen(prefix) */
	const char *replacement; /* e.g. "--specs=" */
} prefix_map[] = {
	{"-specs=",  7, "--specs="},
	{"-target=", 8, "--target="},
	{"-std=",    5, "--std="},
	{NULL, 0, NULL}
};

/* Normalize argv: replace single-dash multi-letter options with
 * double-dash equivalents. Returns newly-allocated argv array
 * (free with free(); substituted strings themselves are leaked).
 * argc is unchanged. argv[0] (program name) is preserved as-is. */
char **
arg_normalize(int argc, char **argv)
{
	char **na;
	int i, j;

	na = malloc((argc + 1) * sizeof(char *));
	if (!na)
		abort();
	for (i = 0; i < argc; i++) {
		char *a = argv[i];
		char *repl = NULL;

		/* skip argv[0] (program name) and non-option args */
		if (i == 0 || a[0] != '-' || a[1] == '\0') {
			na[i] = a;
			continue;
		}

		/* leave "--" terminator and existing --xxx long options alone */
		if (a[0] == '-' && a[1] == '-') {
			na[i] = a;
			continue;
		}

		/* leave single-letter category prefixes alone:
		 * -W<warning>, -f<feature>, -m<machine>, -M<dep>,
		 * -D/U/I/L/l/O/g/w/v/t/P/H (single char or single char + value).
		 * These are not multi-letter options. */
		if (a[1] != '\0' && a[2] == '\0') {
			/* pure single-letter: -c, -o, -S, ... */
			na[i] = a;
			continue;
		}
		/* single-letter prefix + attached value: -Dfoo, -I/usr, -O2, -Wall,
		 * -fPIC, -march=native, -MD, -MFfile, etc. First char is the
		 * category marker. Leave as-is. */
		if (a[1] == 'W' || a[1] == 'f' || a[1] == 'm' ||
		    a[1] == 'M' || a[1] == 'D' || a[1] == 'U' ||
		    a[1] == 'I' || a[1] == 'L' || a[1] == 'l' ||
		    a[1] == 'O' || a[1] == 'o' || a[1] == 't' ||
		    a[1] == 'g' || a[1] == 'v' || a[1] == 'w' ||
		    a[1] == 'P' || a[1] == 'H' || a[1] == 'F' ||
		    a[1] == 'T' || a[1] == 'Q') {
			na[i] = a;
			continue;
		}

		/* exact match (no =value) */
		for (j = 0; exact_map[j].short_form; j++) {
			if (strcmp(a, exact_map[j].short_form) == 0) {
				repl = (char *)exact_map[j].long_form;
				break;
			}
		}

		/* bare multi-letter option taking separated arg */
		if (!repl) {
			for (j = 0; bare_arg_map[j].short_form; j++) {
				if (strcmp(a, bare_arg_map[j].short_form) == 0) {
					repl = (char *)bare_arg_map[j].long_form;
					break;
				}
			}
		}

		/* prefix match (-specs=X -> --specs=X) */
		if (!repl) {
			for (j = 0; prefix_map[j].prefix; j++) {
				if (strncmp(a, prefix_map[j].prefix,
				            prefix_map[j].len) == 0) {
					size_t restlen = strlen(a) - prefix_map[j].len;
					repl = malloc(strlen(prefix_map[j].replacement)
					              + restlen + 1);
					if (!repl)
						abort();
					strcpy(repl, prefix_map[j].replacement);
					strcat(repl, a + prefix_map[j].len);
					break;
				}
			}
		}

		na[i] = repl ? repl : a;
	}
	na[argc] = NULL;
	return na;
}
