/* getopt.h — GNU getopt_long / getopt_long_only API.
 *
 * The POSIX getopt() is declared in <unistd.h>.  This header adds the
 * GNU extension long-option forms.
 *
 * Declarations here are in the compat layer's public include path so that
 * programs using _GNU_SOURCE can #include <getopt.h> and expect the full
 * GNU getopt API.  The implementation lives in src/process/getopt_long.c
 * and is part of libc-meuos.a (core, not compat), because the symbol names
 * are non-standard but widely depended on, and placing them in core avoids
 * link-order issues when compat archives are not linked.
 */

#ifndef MEUOS_GETOPT_H
#define MEUOS_GETOPT_H

#include <features.h>

/* GNU getopt option descriptor used by getopt_long / getopt_long_only. */
struct option {
	const char *name;
	int has_arg;
	int *flag;
	int val;
};

/* Values for has_arg. */
#define no_argument       0
#define required_argument 1
#define optional_argument 2

__BEGIN_DECLS
int getopt_long(int, char *const[], const char *,
                const struct option *, int *);
int getopt_long_only(int, char *const[], const char *,
                     const struct option *, int *);
__END_DECLS

#endif /* MEUOS_GETOPT_H */