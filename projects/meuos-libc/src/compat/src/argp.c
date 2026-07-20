#include <argp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

/* Minimal argp_parse: iterate argv, dispatch options, collect positionals.
 * Handles --opt=val, --opt val, -oval, -o val, --, --help, --usage, -?,
 * and short option clustering.  Errors go to stderr via argp_error. */

#define OPT_HELP  -2
#define OPT_USAGE -3

static int
match_long(const struct argp_option *opts, const char *name, size_t len,
    const struct argp_option **out)
{
	int i;

	for (i = 0; opts[i].name || opts[i].key; ++i) {
		if (!opts[i].name)
			continue;
		if (len == strlen(opts[i].name)
		 && strncmp(opts[i].name, name, len) == 0) {
			*out = &opts[i];
			return 0;
		}
	}
	return -1;
}

void
argp_usage(const struct argp_state *state)
{
	(void)state;
	fprintf(stderr, "Try --help for more information.\n");
}

void
argp_error(const struct argp_state *state, const char *format, ...)
{
	va_list ap;

	(void)state;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
	fprintf(stderr, "\n");
}

void
argp_failure(const struct argp_state *state, int status, int errnum,
    const char *format, ...)
{
	va_list ap;

	(void)state;
	if (format) {
		va_start(ap, format);
		vfprintf(stderr, format, ap);
		va_end(ap);
	}
	if (errnum)
		fprintf(stderr, ": %s", strerror(errnum));
	fprintf(stderr, "\n");
	if (status)
		exit(status);
}

static void
print_help(const struct argp *argp, const char *name)
{
	int i;

	if (argp->doc)
		printf("%s\n", argp->doc);
	printf("Usage: %s [OPTIONS...] %s\n\n",
	    name ? name : "program",
	    argp->args_doc ? argp->args_doc : "");
	for (i = 0; argp->options[i].name || argp->options[i].key; ++i) {
		const struct argp_option *o = &argp->options[i];

		if (o->name)
			printf("  --%s%s  %s\n", o->name,
			    o->arg ? "=" : "",
			    o->arg ? o->arg : "");
		else if (o->key > 32 && o->key < 127)
			printf("  -%c%s  %s\n", o->key,
			    o->arg ? " " : "",
			    o->arg ? o->arg : "");
		if (o->doc)
			printf("      %s\n", o->doc);
	}
}

void
argp_help(const struct argp *argp, FILE *stream, unsigned flags,
    const char *name)
{
	(void)flags;
	(void)stream;
	print_help(argp, name);
}

error_t
argp_parse(const struct argp *argp, int argc, char **argv,
    unsigned flags, int *arg_index, void *input)
{
	struct argp_state state;
	int i = 1;
	int positional = 0;

	memset(&state, 0, sizeof(state));
	state.input = input;
	state.name = argv[0] ? argv[0] : "program";
	state.err_stream = stderr;
	state.out_stream = stdout;
	if (flags & ARGP_PARSE_ARGV0)
		i = 0;

	while (i < argc) {
		char *arg = argv[i];
		const struct argp_option *opt = NULL;

		if (arg[0] != '-' || arg[1] == '\0') {
			/* Positional argument. */
			error_t r = argp->parser('A' /* ARGP_KEY_ARG */,
			    arg, &state);

			if (r == ARGP_ERR_UNKNOWN) {
				argp_usage(&state);
				return EINVAL;
			}
			if (r)
				return r;
			++positional;
			++i;
			continue;
		}
		if (arg[1] == '-' && arg[2] == '\0') {
			/* "--" terminator: rest are positional. */
			++i;
			while (i < argc) {
				error_t r = argp->parser('A',
				    argv[i], &state);

				if (r == ARGP_ERR_UNKNOWN) {
					argp_usage(&state);
					return EINVAL;
				}
				if (r)
					return r;
				++i;
				++positional;
			}
			break;
		}
		if (arg[1] == '-') {
			/* Long option. */
			char *eq = strchr(arg + 2, '=');
			size_t namelen = eq ? (size_t)(eq - arg - 2)
			    : strlen(arg + 2);
			char *optarg = eq ? eq + 1 : NULL;

			if (match_long(argp->options, arg + 2, namelen, &opt)
			    < 0) {
				fprintf(stderr, "%s: unrecognized option %s\n",
				    state.name, arg);
				argp_usage(&state);
				return EINVAL;
			}
			if (!optarg && opt->arg && !(opt->flags & 0x1)) {
				if (++i >= argc) {
					fprintf(stderr,
					    "%s: option --%s requires an argument\n",
					    state.name, opt->name);
					return EINVAL;
				}
				optarg = argv[i];
			}
			if (opt->key == OPT_HELP) {
				print_help(argp, state.name);
				exit(0);
			}
			{
				error_t r = argp->parser(opt->key, optarg, &state);

				if (r == ARGP_ERR_UNKNOWN) {
					argp_usage(&state);
					return EINVAL;
				}
				if (r)
					return r;
			}
			++i;
			continue;
		}
		/* Short options (possibly clustered). */
		{
			int j = 1;

			while (arg[j]) {
				int k;
				const struct argp_option *so = NULL;

				for (k = 0; argp->options[k].name
				    || argp->options[k].key; ++k) {
					if (argp->options[k].key
					    == (unsigned char)arg[j]) {
						so = &argp->options[k];
						break;
					}
				}
				if (!so) {
					fprintf(stderr,
					    "%s: invalid option -%c\n",
					    state.name, arg[j]);
					argp_usage(&state);
					return EINVAL;
				}
				if (so->arg) {
					char *optarg = (arg[j + 1])
					    ? &arg[j + 1] : NULL;

					if (!optarg) {
						if (++i >= argc) {
							fprintf(stderr,
							    "%s: option -%c requires an argument\n",
							    state.name, arg[j]);
							return EINVAL;
						}
						optarg = argv[i];
					}
					{
						error_t r = argp->parser(
						    so->key, optarg, &state);

						if (r == ARGP_ERR_UNKNOWN) {
							argp_usage(&state);
							return EINVAL;
						}
						if (r)
							return r;
					}
					break;
				}
				{
					error_t r = argp->parser(so->key,
					    NULL, &state);

					if (r == ARGP_ERR_UNKNOWN) {
						argp_usage(&state);
						return EINVAL;
					}
					if (r)
						return r;
				}
				++j;
			}
		}
		++i;
	}
	{
		error_t r = argp->parser(0 /* ARGP_KEY_END */, NULL, &state);

		if (r && r != ARGP_ERR_UNKNOWN)
			return r;
	}
	if (arg_index)
		*arg_index = i;
	return 0;
}
