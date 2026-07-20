/*
 * arg.h - argv option parser
 *
 * ARGBEGIN/ARGEND iterate over short options bundled in argv entries
 * (e.g. -abc, -oFILE). Two extensions over the original sbase macro:
 *
 *   1. Long options: an argument of the form "--name" (with a non-empty
 *      name; the bare "--" terminator is still handled separately) is
 *      exposed to the switch as the single char '-', so a driver can
 *      handle long options with `case '-':` by inspecting *argv.
 *
 *   2. EARGF remains available for options that take an argument, which
 *      may be attached (-oFILE) or the next argv (-o FILE).
 *
 * Drivers that need multi-character whole-arg options (e.g. -std=,
 * -static, -MD <file>) may also examine *argv directly inside the
 * matching case and set done_ = 1 to consume the whole argument.
 *
 * Multi-letter options should use the double-dash standard form
 * (--static, --shared, --nostdlib, ...). The gcc/clang traditional
 * single-dash forms (-static, -shared, ...) are normalized to
 * double-dash by arg_normalize() in src/driver/arg_compat.c before
 * the main parser runs. Single-letter short options (-c, -o, -S, ...)
 * and category-prefixed options (-W<w>, -f<f>, -m<m>) stay single-dash.
 */

/* arg_compat.c - gcc/clang compatibility shim */
char **arg_normalize(int argc, char **argv);

#define ARGBEGIN \
	for (;;) { \
		if (argc > 0) \
			++argv, --argc; \
		if (argc == 0 || (*argv)[0] != '-') \
			break; \
		if ((*argv)[1] == '-' && !(*argv)[2]) { \
			++argv, --argc; \
			break; \
		} \
		for (char *opt_ = ((*argv)[1] == '-' ? *argv + 1 : &(*argv)[1]), done_ = 0; !done_ && *opt_; ++opt_) { \
			switch (*opt_)

#define ARGEND \
		} \
	}

#define EARGF(x) \
	(done_ = 1, opt_[1] ? ++opt_ : argv[1] ? --argc, *++argv : ((x), abort(), (char *)0))
