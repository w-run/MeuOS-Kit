#ifndef _MONEY_H
#define _MONEY_H

#include <features.h>
#include <stddef.h>
#include <sys/types.h>

/* POSIX.1-2008 monetary value formatting.
 * Format: %[flags=^+(-! ][field-width][.precision][l?]n          (national)
 *                ...     i                                        (international)
 * Returns bytes written to s (excluding NUL), or -1 if s would overflow. */
__BEGIN_DECLS
ssize_t strfmon(char *restrict s, size_t maxsize, const char *restrict format, ...);
__END_DECLS

#endif
