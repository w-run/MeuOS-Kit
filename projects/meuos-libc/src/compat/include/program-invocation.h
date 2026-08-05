#ifndef MEUOS_COMPAT_PROGRAM_INVOCATION_H
#define MEUOS_COMPAT_PROGRAM_INVOCATION_H

/* GNU/glibc-convention program-invocation globals.
 *
 * glibc declares these in <errno.h> and publishes them from its own libc
 * startup.  MeuOS defines and populates both pointers in the core library
 * (src/startup.c, from argv[0]) because only libc startup can, and this crt
 * has no .init_array for an add-on archive to hook into.  This header is the
 * opt-in compat declaration surface (core does not pretend to be glibc), and
 * keeps the values out of the core public headers.
 */
extern char *program_invocation_name;
extern char *program_invocation_short_name;

#endif
