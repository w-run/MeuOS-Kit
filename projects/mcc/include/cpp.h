/* cpp.h — m++ (C++) frontend public API.
 *
 * Shared header for the C++ frontend modules (src/cpp/), mirroring the
 * role mcc.h plays for the C frontend.  m++ compiles C++ sources by
 * running this frontend and lowering to MIR (src/mir/), sharing the
 * backend (libmcc.a) with mcc.
 */
#ifndef MCC_CPP_H
#define MCC_CPP_H

struct qualtype;   /* defined in src/parse/decl_internal.h */

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
    struct expr *args, char *buf, size_t bufsz, bool prefer_ref);
struct decl *cpp_find_unique_member(struct type *t, const char *name,
    char *mname, size_t mname_sz);
void cpp_parse_free_operator(struct scope *s, struct qualtype base);
struct expr *cpp_member_ident(struct scope *s, const char *name);
struct decl *cpp_lookup_visible(struct scope *s, const char *name);
struct expr *cpp_temp_construct(struct scope *s, struct type *ct);
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
bool cpp_member_ambiguous(struct type *t, const char *name);
void cpp_set_qual_class(const char *tag);
const char *cpp_take_qual_class(void);
void cpp_set_qual_ns(struct scope *ns);
struct scope *cpp_take_qual_ns(void);
void cpp_define_static_data(struct scope *s, const char *qclass,
    const char *name);
void cpp_record_global_ctor(struct decl *d, struct expr *args);

/* C++ virtual functions / vtable (C.2.5) ------------------------------ */

/* One slot in a class's vtable layout.  `key` identifies the virtual
 * method across the hierarchy (method name + trailing-const + encoded
 * parameter types); an override reuses the base slot with the same key. */
struct cpp_vslot {
	const char *name;      /* method name (persistent) */
	char key[256];         /* signature identity (name + K? + param codes) */
	struct member *m;      /* the member (set when the member is added) */
	struct type *owner;    /* class that owns the slot layout */
	int index;             /* slot index in the full vtable layout */
	struct cpp_vslot *next;
};

/* Is the named member of `t` a virtual function? */
bool cpp_is_virtual(struct type *t, const char *name);
/* Slot index of virtual member `m` in its declaring class's vtable. */
int cpp_vslot_index(struct type *t, struct member *m);
/* Class that declares the member function `name` of `t` (defining base
 * for inherited methods), or NULL. */
struct type *cpp_method_owner(struct type *t, const char *name);
/* Byte offset of the `base` subobject within a complete object of class
 * `derived` (0 when derived == base or base is the primary base). */
unsigned long long cpp_base_offset(struct type *derived, struct type *base);
/* Build the indirect callable for a virtual call: load the vptr from
 * `thisp`, index the slot, cast to the method's function-pointer type
 * (this + explicit params).  The call lowering prepends `thisp`. */
struct expr *cpp_make_vcall(struct expr *thisp, struct type *owner,
    struct member *m, int slot);
/* Emit every polymorphic class's vtable data (called at end of TU). */
void cpp_emit_vtables(void);

/* C++ function templates (C.2.8): instantiate-on-first-use.  primaryexpr
 * consults cpp_tmpl_lookup / cpp_tmpl_placeholder for an undeclared callee;
 * the TLPAREN lowering calls cpp_tmpl_instantiate once the argument types
 * are known. */
const char *cpp_tmpl_lookup(const char *name);
struct expr *cpp_tmpl_placeholder(const char *name);
struct expr *cpp_tmpl_instantiate(struct scope *s, struct expr *arglist);

#endif /* MCC_CPP_H */
