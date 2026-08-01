/* cpp.h — m++ (C++) frontend public API.
 *
 * Shared header for the C++ frontend modules (src/cpp/), mirroring the
 * role mcc.h plays for the C frontend.  m++ compiles C++ sources by
 * running this frontend and lowering to MIR (src/mir/), sharing the
 * backend (libmcc.a) with mcc.
 */
#ifndef MCC_CPP_H
#define MCC_CPP_H

#include <stddef.h>

#include "cpp/cpp_tokens.h"

/* lex (src/cpp/lex/cpp_scan.c) */
enum cpp_tokenkind cpp_classify_ident(const char *name, size_t len);
int cpp_nkeywords(void);

/* parse (src/cpp/parse/cpp_parse.c) */
void cpp_parse_translation_unit(void);
enum cpp_tokenkind cpp_tok_kind(void);

/* C++ member-function lowering helpers (src/cpp/parse/cpp_parse.c).
 * cpp_is_member_function: is `name` a function member of struct `t`?
 * cpp_mangled_name: returns `Class_method` mangled name in buf.
 * cpp_member_ident: resolve a bare class-member name inside a method
 * body (`count` -> `(*this).count`, `reset()` -> `Class_reset(this)`);
 * returns NULL when `name` is not a member of the current method class. */
bool cpp_is_member_function(struct type *t, const char *name);
const char *cpp_mangled_name(struct type *t, const char *name,
    char *buf, size_t bufsz);
struct expr *cpp_member_ident(struct scope *s, const char *name);
bool cpp_has_ctor(struct type *t, const char *tag);
bool cpp_emit_default_ctor(struct func *f, struct decl *d);
bool cpp_same_class_context(struct type *t);
bool cpp_member_accessible(struct type *t, struct member *m);
bool cpp_is_derived(struct type *t, struct type *base);
void cpp_set_qual_class(const char *tag);
const char *cpp_take_qual_class(void);

#endif /* MCC_CPP_H */
