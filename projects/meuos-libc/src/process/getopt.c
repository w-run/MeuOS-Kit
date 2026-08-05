/* process/getopt.c — POSIX.1-2008 command-line option parsing (getopt).
 *
 * getopt(argc, argv, optstring) returns the next short option letter, '?' on
 * an unknown option or a missing required argument, ':' when optstring
 * starts with ':' and a required argument is missing, or -1 at the end.
 * optind starts at 1 and, once parsing ends, points at the first operand.
 * Grouped short options (-abc == -a -b -c) are supported; an option that
 * takes an argument consumes the rest of its argv element or the next
 * element into optarg.  A leading "--" and a lone "-" terminate scanning.
 * An initial ':' suppresses error output (and selects the ':' return).
 * Zero GNU dependency; getopt is POSIX.1-2008 in core libc. */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

char *optarg;
int optind = 1;
int opterr = 1;
int optopt;

static int optwhere = 1;   /* index of the next option char in argv[optind] */
static int last_optind = 1; /* argv index optwhere belongs to */

int
getopt(int argc, char *const argv[], const char *optstring)
{
	optarg = NULL;

	/* The caller may reset optind to 1 (POSIX restart) to scan a fresh argv
	 * array; detect an externally-moved optind and reinitialize optwhere. */
	if (optind != last_optind) {
		optwhere = 1;
		last_optind = optind;
	}

	/* process options.  We do not permute argv (POSIX allows ending the scan
	 * at the first operand); optind ends pointing at the first operand. */
	if (optind >= argc || !argv[optind])
		return -1;

	for (;;) {
		const char *arg = argv[optind];
		/* end conditions */
		if (arg[0] != '-' || arg[1] == '\0')
			return -1;                 /* operand, or lone '-' */
		if (arg[1] == '-' && arg[2] == '\0') {
			optind++;                  /* "--": explicit end */
			last_optind = optind;
			return -1;
		}
		/* a real option token (-a, -abc, -aX, ...): scan it */
		if (optwhere >= (int)strlen(arg)) {
			/* exhausted this token: move to the next argv element */
			optind++;
			last_optind = optind;
			optwhere = 1;
			if (optind >= argc || !argv[optind])
				return -1;
			continue;                  /* re-evaluate next token */
		}

		char c = arg[optwhere++];
		const char *p = optstring ? strchr(optstring, c) : NULL;
		if (!p || c == ':') {
			optopt = c;
			if (opterr && optstring && optstring[0] != ':')
				fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0], c);
			return '?';
		}

		int need = (p[1] == ':');
		int opta = need && (p[2] == ':');

		if (need) {
			/* argument required or optional */
			size_t elen = strlen(arg);
			if (optwhere < (int)elen) {
				/* argument is the rest of this token */
				optarg = (char *)(arg + optwhere);
				optwhere = (int)elen;      /* this token is now fully consumed */
			} else if (!opta) {
				/* argument is the next element (required) */
				if (optind + 1 < argc && argv[optind + 1]) {
					optarg = argv[optind + 1];
					optind += 2;           /* past the option and its argument */
					last_optind = optind;
					optwhere = 1;          /* next call scans a fresh token */
					/* this option consumed the whole current token too; the
					 * argument-bearing element is consumed as well. */
					return c;
				} else {
					optopt = c;
					if (opterr && optstring && optstring[0] != ':')
						fprintf(stderr,
						        "%s: option requires an argument -- '%c'\n",
						        argv[0], c);
					return (optstring && optstring[0] == ':') ? ':' : '?';
				}
			}
			/* else: optional argument (::) and none present -> optarg stays
			 * NULL and optargument handling is done. */
		}

		return c;
	}
}
