/* cpp_internal.h - internal cross-file declarations shared by the C++
 * front-end modules under src/cpp/parse/.  Split out of cpp_parse.c so
 * the large file could be layered into per-domain submodules while
 * keeping shared globals/functions visible across translation units.
 *
 * Only symbols that genuinely need to be visible to more than one .c
 * file live here; per-file static state stays static in its own file. */
#ifndef MCC_CPP_PARSE_INTERNAL_H
#define MCC_CPP_PARSE_INTERNAL_H

#include "cpp.h"

/* Non-type template parameter values captured for constexpr evaluation.
 * Shared between the template-instantiation code (cpp_parse.c) and the
 * constexpr evaluator (cpp_constexpr.c). */
extern const char *g_cpp_cexpr_tmpl_params[16];
extern struct type *g_cpp_cexpr_tmpl_types[16];
extern unsigned long long g_cpp_cexpr_tmpl_vals[16];
extern bool g_cpp_cexpr_tmpl_isval[16];
extern int g_cpp_cexpr_tmpl_n;

#endif /* MCC_CPP_PARSE_INTERNAL_H */
