/* process/getopt_long.c — GNU getopt_long / getopt_long_only.
 *
 * Implements the GNU extension forms by piggybacking on the POSIX getopt()
 * infrastructure (optarg/optind/opterr/optopt defined in src/process/getopt.c).
 * This is kept in core libc (not compat) so that it is always available for
 * programs compiled against the meuos sysroot.
 *
 * Based on the public-domain musl implementation. */

#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

extern char *optarg;
extern int optind, opterr, optopt;

static int
__getopt_long(int argc, char *const argv[], const char *optstring,
              const struct option *longopts, int *longidx, int short_only)
{
	char *arg;

	/* Reset? */
	if (optind == 0) optind = 1;
	if (optind >= argc || !argv[optind] || argv[optind][0] != '-')
		return -1;

	if (argv[optind][0] == '-' && argv[optind][1] == '-' && !argv[optind][2]) {
		optind++;
		return -1;
	}

	if (short_only || argv[optind][0] != '-' || argv[optind][1] == '-')
		goto short_opt;

	/* Parse long option */
	const char *name = argv[optind] + 2;
	const char *eq = strchr(name, '=');
	size_t nlen = eq ? (size_t)(eq - name) : strlen(name);
	int match = -1;
	int partial = 0;

	if (!longopts) goto short_opt;

	for (int i = 0; longopts[i].name; i++) {
		if (!strncmp(name, longopts[i].name, nlen)
		    && strlen(longopts[i].name) == nlen) {
			match = i;
			partial = 0;
			break;
		}
		if (!strncmp(name, longopts[i].name, nlen))
			match = i, partial = 1;
	}

	if (match < 0 || partial) {
		if (opterr)
			fprintf(stderr, "%s: option '%s' is ambiguous\n",
			        argv[0], name);
		optopt = 0;
		return '?';
	}

	if (longidx) *longidx = match;

	arg = eq ? (char *)(eq + 1) : NULL;
	int argerror = 0;

	switch (longopts[match].has_arg) {
	case no_argument:
		if (eq) argerror = 1;
		break;
	case required_argument:
		if (!arg) {
			if (optind < argc - 1)
				arg = argv[++optind];
			else
				argerror = 1;
		}
		break;
	case optional_argument:
		if (!arg && optind < argc - 1 && argv[optind+1][0] != '-')
			arg = argv[++optind];
		break;
	}

	if (argerror) {
		if (opterr)
			fprintf(stderr, "%s: option '%s' requires an argument\n",
			        argv[0], longopts[match].name);
		optopt = longopts[match].val;
		return '?';
	}

	if (longopts[match].flag) {
		*longopts[match].flag = longopts[match].val;
		return 0;
	}
	optarg = arg;
	return longopts[match].val;

short_opt:
	/* Reuse POSIX getopt fallthrough. */
	return getopt(argc, argv, optstring);
}

int
getopt_long(int argc, char *const argv[], const char *optstring,
            const struct option *longopts, int *longidx)
{
	return __getopt_long(argc, argv, optstring, longopts, longidx, 0);
}

int
getopt_long_only(int argc, char *const argv[], const char *optstring,
                 const struct option *longopts, int *longidx)
{
	return __getopt_long(argc, argv, optstring, longopts, longidx, 1);
}