#ifndef MEUOS_COMPAT_GLIBC_COMPAT_H
#define MEUOS_COMPAT_GLIBC_COMPAT_H

/* glibc-compat.h — opt-in glibc-compatibility surface.
 *
 * MeuOS core deliberately does not masquerade as glibc (layering rule).
 * Some autotools/third-party sources probe `__GLIBC__` in <features.h> and
 * refuse to build without it.  To make such code work against MeuOS while
 * keeping core pure, this compat header (or the companion
 * `-D__MEUOS_GLIBC_COMPAT__` from `meuos-glibc-compat.pc`) arms the guarded
 * __GLIBC__ block inside core <features.h>.
 *
 * Preferred usage (reliable for autotools, which include <features.h> before
 * anything else): add the pkg-config flag to the compile command, e.g.
 *     cc $(pkg-config --cflags meuos-glibc-compat) app.c
 * which injects -D__MEUOS_GLIBC_COMPAT__ before any #include.
 *
 * Alternative: include this header before any other libc header:
 *     #include <glibc-compat.h>
 *     #include <features.h>   /* now sees __MEUOS_GLIBC_COMPAT__ */
 */

#ifndef __MEUOS_GLIBC_COMPAT__
# define __MEUOS_GLIBC_COMPAT__ 1
#endif

#endif /* MEUOS_COMPAT_GLIBC_COMPAT_H */
