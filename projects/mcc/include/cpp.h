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
const char *cpp_op_mangle(enum tokenkind op);
bool cpp_try_operator_call(struct scope *s, struct expr *l,
    enum tokenkind op, struct expr *r, struct expr **out);
const char *cpp_mangled_name(struct type *t, const char *name,
    char *buf, size_t bufsz);
void cpp_mangled_name_args(struct type *t, const char *name,
    struct expr *args, char *buf, size_t bufsz);
struct expr *cpp_member_ident(struct scope *s, const char *name);
struct decl *cpp_lookup_visible(struct scope *s, const char *name);
extern struct type *g_cpp_member_class;
extern const char *g_cpp_member_name;
extern bool g_cpp_member_const;
extern int g_cpp_postfix_depth;
void cpp_pending_record_depth(void);
void cpp_pending_clear_at_depth(int depth);
void cpp_pending_set_placeholder(void);
bool cpp_pending_was_placeholder(void);
bool cpp_pending_is_mine(int depth);
bool cpp_has_ctor(struct type *t, const char *tag);
bool cpp_emit_default_ctor(struct func *f, struct decl *d);
bool cpp_has_dtor(struct type *t);
bool cpp_emit_dtor(struct func *f, struct decl *d);
void cpp_emit_scope_dtors(struct func *f, struct scope *s);
void cpp_emit_ctor_call(struct func *f, struct decl *d, struct expr *args);
void cpp_ctor_args_begin(void);
void cpp_ctor_args_add(struct expr *e);
struct expr *cpp_ctor_args_take(void);
void cpp_ctor_set_active(void);
bool cpp_ctor_is_active(void);
void cpp_ctor_clear_active(void);
bool cpp_same_class_context(struct type *t);
bool cpp_member_accessible(struct type *t, struct member *m);
bool cpp_is_derived(struct type *t, struct type *base);
void cpp_set_qual_class(const char *tag);
const char *cpp_take_qual_class(void);
void cpp_set_qual_ns(struct scope *ns);
struct scope *cpp_take_qual_ns(void);

#endif /* MCC_CPP_H */
