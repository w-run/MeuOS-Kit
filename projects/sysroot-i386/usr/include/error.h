#ifndef MEUOS_ERROR_H
#define MEUOS_ERROR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GNU error-reporting API (argp, coreutils, findutils, etc. depend on it).
 * Implemented on top of POSIX stderr + strerror, no glibc internals. */
extern unsigned int error_message_count;
extern int error_one_per_line;
extern void (*error_print_progname)(void);

void error(int status, int errnum, const char *format, ...);
void error_at_line(int status, int errnum, const char *fname,
    unsigned int lineno, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif
