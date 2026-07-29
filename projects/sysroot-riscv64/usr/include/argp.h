#ifndef MEUOS_ARGP_H
#define MEUOS_ARGP_H

#include <stddef.h>
#include <stdio.h>

#ifndef __error_t_defined
#define __error_t_defined
typedef int error_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Minimal argp: a glibc-derived argument parser used by many GNU tools.
 * This implementation covers --help, --version, short/long option parsing
 * with optional arguments, and positional arguments.  It does NOT support
 * the full argp feature set (option aliases, parsers hierarchy, etc.). */

struct argp_option {
	const char *name;
	int key;
	const char *arg;
	int flags;
	const char *doc;
	int group;
};

#define ARGP_OPTION_ARG_OPTIONAL 0x1
#define ARGP_OPTION_HIDDEN       0x2
#define ARGP_OPTION_ALIAS        0x4
#define ARGP_DOC_(__doc) (__doc)

struct argp_state;
typedef error_t (*argp_parser_t)(int key, char *arg, struct argp_state *state);

struct argp {
	const struct argp_option *options;
	argp_parser_t parser;
	const char *args_doc;
	const char *doc;
};

#define ARGP_ERR_UNKNOWN 0x0e72d910  /* EAGAIN-ish sentinel */

struct argp_state {
	void *input;
	int next;
	unsigned int flags;
	int arg_num;
	unsigned int quoted;
	void *hook;
	const char *name;
	FILE *err_stream;
	FILE *out_stream;
	void *pstate;
};

#define ARGP_PARSE_ARGV0 0x01
#define ARGP_NO_ERRS     0x02
#define ARGP_NO_ARGS     0x04
#define ARGP_IN_ORDER    0x08

error_t argp_parse(const struct argp *, int, char **,
    unsigned, int *, void *);
void argp_usage(const struct argp_state *);
void argp_error(const struct argp_state *, const char *, ...);
void argp_help(const struct argp *, FILE *, unsigned, const char *);
void argp_failure(const struct argp_state *, int, int, const char *, ...);

#ifdef __cplusplus
}
#endif

#endif
