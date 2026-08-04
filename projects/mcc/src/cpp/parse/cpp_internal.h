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

/* C++17 fold-expression expansion: scan `toks[0..n)` for folder shapes
 * involving `pack_var` and emit an expanded token stream.  Called by the
 * requires-expression / template-argument code in cpp_parse.c. */
void cpp_expand_folds(struct token *toks, size_t n, const char *pack_var,
                      int npack, struct token **out, size_t *outn);

/* Pending `this` object of the next member-function call, set by the
 * postfix `.`/`->` lowering; the lambda lowering nulls it out. */
extern struct expr *g_cpp_member_this;

/* Running counter for synthesized closure classes ``__lambdaN``; shared
 * between the template-instantiation code (cpp_parse.c) and the lambda
 * lowering (cpp_lambda.c). */
extern int g_cpp_lambda_count;

/* Parse a C++ ``class``/``struct`` body (access-control sections,
 * members, base lists).  Defined in cpp_parse.c; the lambda lowering
 * replays a synthesized closure-class definition through it. */
bool cpp_class_decl(struct scope *s);

/* Token-stream builder for the synthesized closure-class definition.
 * Defined in cpp_lambda.c; used by the template-declaration code in
 * cpp_parse.c. */
void cpp_tb(struct token *buf, size_t *n, struct token tmpl,
            enum tokenkind k, const char *name);

/* C++ name mangling (defined in cpp_mangle.c). */
void cpp_mangle_type(struct type *t, char *buf, size_t bufsz);

/* Resolve the class that owns method `name` of type `t` (following
 * inheritance); defined in cpp_parse.c, used by the mangle module. */
struct member *cpp_method_member(struct type *t, const char *name,
                                 struct type **owner);

#endif /* MCC_CPP_PARSE_INTERNAL_H */
