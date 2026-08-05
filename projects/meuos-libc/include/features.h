#ifndef _FEATURES_H
#define _FEATURES_H	1

/* MeuOS feature-test-macro switchboard.
 *
 * Decodes the traditional glibc-style feature test macros into thin
 * `__USE_*` capability flags that individual headers use to decide which
 * declarations to expose.  Present in the CORE library, but does **no**
 * __GLIBC__ masquerading: those globals/ABIs are a compat-layer concern.
 *
 * DEFAULT POLICY (musl-leaning, chosen to keep the existing toolchain
 * self-bootstrap unbroken):
 *   - `_GNU_SOURCE`  (defined)      -> broadest view, incl. GNU/XSI.
 *   - `_DEFAULT_SOURCE`             -> default/all POSIX + misc.
 *   - `_POSIX_C_SOURCE`/_XOPEN_SOURCE -> POSIX-only views (points-in-time).
 *   - nothing defined               -> the full default view, so pre-existing
 *     mcc/meow builds that relied on all symbols keep compiling.
 * This is the skeleton layer: it computes flags but does not yet hide any
 * symbol; per-header gating lands incrementally later so self-bootstrap
 * never regresses.
 */

#if defined(_GNU_SOURCE)
# define __USE_GNU 1
# define __USE_MISC 1
# define __USE_POSIX 1
# define __USE_POSIX2 1
# define __USE_POSIX199309 1
# define __USE_POSIX199506 1
# define __USE_XOPEN 1
# define __USE_XOPEN2K 1
# define __USE_XOPEN2K8 1
# define __USE_XOPEN_EXTENDED 1
# define __USE_UNIX98 1
#elif defined(_DEFAULT_SOURCE)
# define __USE_MISC 1
# define __USE_POSIX 1
# define __USE_POSIX2 1
# define __USE_POSIX199309 1
# define __USE_POSIX199506 1
# define __USE_XOPEN2K 1
# define __USE_XOPEN2K8 1
# define __USE_UNIX98 1
#else
/* Nothing defined: expose the full default view (safe for self-bootstrap). */
# define __USE_MISC 1
# define __USE_POSIX 1
# define __USE_POSIX2 1
# define __USE_POSIX199309 1
# define __USE_POSIX199506 1
# define __USE_XOPEN 1
# define __USE_XOPEN2K 1
# define __USE_XOPEN2K8 1
# define __USE_XOPEN_EXTENDED 1
# define __USE_UNIX98 1
# define __USE_GNU 1
#endif

/* ---- exported capability numbers ---- */

/* POSIX version the library implements (also returned by sysconf). */
#define _POSIX_VERSION 200809L

/* XSI (X/Open System Interfaces) version. */
#define _XOPEN_VERSION 700

/* ---- linkage helpers (used by many headers) ---- */
#ifdef __cplusplus
# define __BEGIN_DECLS extern "C" {
# define __END_DECLS }
#else
# define __BEGIN_DECLS
# define __END_DECLS
#endif

#endif /* _FEATURES_H */
