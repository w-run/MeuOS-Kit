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
struct decl;

#include <stddef.h>

#include "cpp/cpp_tokens.h"

/* lex (src/cpp/lex/cpp_scan.c) */
enum cpp_tokenkind cpp_classify_ident(const char *name, size_t len);
int cpp_nkeywords(void);

/* parse (src/cpp/parse/cpp_parse.c) */
void cpp_parse_translation_unit(void);
void cpp_using_decl(struct scope *s);
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
/* Lower `obj[args...]` to `obj.operator_ix(args...)` (C++23 P2128 allows
 * arbitrary argument counts).  Returns true and sets *out on success. */
bool cpp_subscript_call(struct scope *s, struct expr *obj,
    struct expr *args, struct expr **out);
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
struct expr *cpp_temp_construct_braced(struct scope *s, struct type *ct);
struct expr *cpp_braced_args_collect(struct scope *s);
void cpp_init_vptrs(struct func *f, struct type *t, struct expr *thisp);
extern struct type *g_cpp_member_class;
extern const char *g_cpp_member_name;
extern bool g_cpp_member_const;
extern bool g_cpp_member_rvalue; /* object expression is a temporary (rvalue) */
extern int g_cpp_postfix_depth;
void cpp_pending_record_depth(void);
void cpp_pending_clear_at_depth(int depth);
/* deducing-this (P0847): the declarator signals a `this X& self` explicit
 * object parameter before parsing it; cpp_define_method consumes it and
 * substitutes the object parameter for the implicit `this`. */
void cpp_explicit_obj_begin(void);
void cpp_explicit_obj_set(struct decl *d);
struct decl *cpp_explicit_obj_take(void);
/* `this` expression of the method body being parsed (NULL outside a
 * method body); deducing-this methods resolve it to &(*self). */
struct expr *cpp_this_expr(void);
void cpp_pending_set_placeholder(void);
bool cpp_pending_was_placeholder(void);
bool cpp_pending_is_mine(int depth);
/* C++14 `auto` return type: record the return expression type of an
 * `auto f() {...}` body; the first return fixes the deduced type. */
void cpp_auto_return(struct func *f, struct expr *e);
/* Number of elements in the innermost replaying template parameter pack
 * (for `sizeof...(Args)`); 0 outside a variadic instantiation. */
int cpp_sizeof_pack(void);
/* Parse a C++11 lambda expression and lower it to a closure object of a
 * synthesized anonymous class (primaryexpr's `[` entry point). */
struct expr *cpp_lambda_expr(struct scope *s);
/* C++ constexpr functions: buffer the body of a constexpr function (called
 * from decl() with tok on '{') and fold a constant-context call. */
void cpp_buffer_constexpr_body(struct decl *d);
struct expr *cpp_constexpr_eval(struct expr *expr);
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
/* Class templates: is `name` a `template<...> class Foo { ... }`? */
const char *cpp_tmpl_class_lookup(const char *name);
/* Instantiate `Foo<...>` (tok is positioned at '<' on entry); parses the
 * explicit template arguments, defines the instantiated class, and returns
 * its type. */
struct type *cpp_tmpl_class_instantiate(struct scope *s, const char *name);
/* C++17 CTAD: `Vec v(a, b)` — deduce the template arguments of class
 * template `name` from the constructor-call argument types and define the
 * instantiated class. */
struct type *cpp_tmpl_class_ctad(struct scope *s, const char *name,
    struct expr *args);
/* C++17 CTAD pending template name, set by declspecs when a class-template
 * name appears without explicit arguments; decl() completes the deduction. */
extern const char *g_cpp_ctad_tmpl;
/* Member templates: is `name` a template member function of class `t`?
 * `cpp_tmpl_member_pend` records the pending member-template call; the
 * TLPAREN lowering calls cpp_tmpl_member_instantiate (with the this
 * object and the explicit arguments) once the argument types are known. */
bool cpp_tmpl_member(struct type *t, const char *name);
void cpp_tmpl_member_pend(struct type *t, const char *name);
struct expr *cpp_tmpl_member_instantiate(struct scope *s,
    struct expr *thisp, struct expr *arglist);
/* Placeholder callee for a pending member-template call (satisfies the
 * TLPAREN "called object" checks until the instantiation replaces it). */
struct expr *cpp_tmpl_member_placeholder(void);

/* C++17 `if constexpr (cond)`: the condition must be a compile-time
 * constant; the selected branch is parsed (via stmt()) and the other is
 * skipped at the token level so it is never instantiated. */
void cpp_if_constexpr(struct func *f, struct expr *cond, struct scope *s);

/* C++20 requires-expression: `requires { ... }` /
 * `requires (params) { reqs... }`.  Parsed in primaryexpr; consumes the
 * whole expression and returns a bool constant (`true` when every
 * requirement is well-formed). */
struct expr *cpp_requires_expr(struct scope *s);

/* C++17 structured binding: `auto [x, y] = expr;` — create a hidden
 * object for the initializer and bind each name to a copy of the
 * corresponding member.  Returns true if handled (tok consumed). */
bool cpp_struct_binding(struct func *f, struct scope *s,
    struct qualtype base);

/* C++ template instantiation (D2): parse the deferred body of a member
 * function that was buffered (but not parsed) during a class-template
 * instantiation, now that the member is actually called.  Called from
 * the generic call emit for any DECLFUNC whose body is still undefined.
 * Returns true when the member is defined afterwards. */
bool cpp_ensure_method_defined(struct decl *fd);

#endif /* MCC_CPP_H */
