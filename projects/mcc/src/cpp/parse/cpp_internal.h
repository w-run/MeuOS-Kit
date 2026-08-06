/* cpp_internal.h - internal cross-file declarations shared by the C++
 * front-end modules under src/cpp/parse/.  Split out of cpp_parse.c so
 * the large file could be layered into per-domain submodules while
 * keeping shared globals/functions visible across translation units.
 *
 * Only symbols that genuinely need to be visible to more than one .c
 * file live here; per-file static state stays static in its own file. */
#ifndef MCC_CPP_PARSE_INTERNAL_H
#define MCC_CPP_PARSE_INTERNAL_H

#include <stdbool.h>
#include "cpp.h"

/* Forward declarations for pointer fields in the template data
 * structures below.  Full definitions live in mcc.h, included by the
 * translation units (not here, to avoid redefining enum tokenkind). */
struct type;
struct token;
struct member;
struct func;
struct scope;
struct expr;
struct decl;
struct cpp_template;
struct location;
struct init;

/* Member-method parsing context: the enclosing class and implicit
 * `this` parameter while a method body is being parsed.  Shared by the
 * member / ctor / operator lowering across the split modules. */
struct cpp_method_ctx {
	struct type *class_type; /* enclosing class of the method being parsed */
	struct decl *this_decl;  /* the implicit `this` parameter decl */
	bool active;
};
extern struct cpp_method_ctx g_cpp_method;

/* Temporary flags during member-function definition (defined in
 * cpp_parse.c): the method being defined is `virtual` or `final`. */
extern bool g_cpp_define_virtual;
extern bool g_cpp_method_final;

/* Two-phase class parsing: buffering/replaying method bodies (defined in
 * cpp_parse.c, shared with the method-body replay module). */
extern bool g_cpp_class_parsing;

/* C++14 auto return-type deduction state: while parsing a method body
 * whose declared return type is `auto`, the first return fixes the type.
 * Defined in cpp_parse.c, used by cpp_parse_method_body in cpp_method.c. */
extern struct type *g_cpp_auto_ret_type;
extern struct func *g_cpp_auto_ret_func;

/* Buffered member-function body, replayed after the class layout is known
 * (so a body may reference members declared later).  Struct defined here
 * (all pointer fields) so the method-body replay module can traverse it. */
struct cpp_pending_method {
	struct token *toks;      /* function-body tokens incl. braces */
	size_t ntoks;
	const char *mname;
	const char *tag;
	struct type *classt;     /* enclosing class */
	struct type *mtype;      /* mangled signature incl. `this` */
	struct decl *thisd;      /* implicit this parameter decl */
	struct decl *d;          /* mangled function decl */
	struct scope *s;         /* class's declaration scope */
	bool is_static;          /* static member: no `this` */
	/* Class-template instantiation context: the template parameter
	 * bindings in effect when this body was buffered; re-installed
	 * before the deferred replay. */
	struct decl *binds[16];
	int nbinds;
	struct cpp_pending_method *next;
};

/* Constructor init-list item: `m(args)` or `Base(args)` in
 * `Derived(int v) : Base(v), m(v * 2) {}`.  Populated by
 * cpp_parse_init_list (called at the start of a ctor body) and consumed
 * by emit_base_ctors_for so an explicit initializer supersedes the
 * implicit default-construction call. */
struct cpp_init_item {
	const char *name;
	struct expr *args; /* linked list of argument expressions */
	struct cpp_init_item *next;
};
extern struct cpp_init_item *g_cpp_init_items;
extern struct cpp_init_item **g_cpp_init_end;

/* Constructor/destructor emission (defined in cpp_ctor.c); the members
 * below are called from the method-body replay in cpp_parse.c. */
void cpp_emit_base_ctor(struct func *f);
void cpp_emit_base_dtor(struct func *f);
void cpp_parse_init_list(struct func *f, struct scope *fs);
void cpp_emit_global_dtor(struct func *f, struct decl *d);

/* Parse a `friend` declaration (defined in cpp_parse.c, class section);
 * used by the access-control check in the operator lowering. */
void cpp_friend_decl(struct scope *s, struct type *classt);

/* Is the current token a `struct`/`union` tag declaration with a base-class
 * list or a body (defined in cpp_parse.c, class section); used by
 * cpp_namespace.c to dispatch nested struct/union in namespace bodies. */
bool cpp_struct_needs_class_decl(void);

/* Classify a token into the C++ keyword kind (defined in cpp_parse.c,
 * class section); used by the requires-expression splitting (cpp_requires.c). */
enum cpp_tokenkind cpp_classify_token(struct token t);

/* requires-expression / constraint machinery (defined in cpp_requires.c):
 * cpp_check_constraint and cpp_requires_span_len are called from the
 * template-instantiation paths in cpp_parse.c. */
bool cpp_check_constraint(struct cpp_template *tmpl, struct scope *bs);
size_t cpp_requires_span_len(struct token **out);

/* Template-instantiation state shared between the member-buffer replay
 * and the class-template instantiation (both in cpp_parse.c / moved
 * modules).  Defined in cpp_parse.c. */
extern bool g_cpp_tmpl_instantiating;
extern struct decl *g_cpp_tmpl_binds[16];
extern int g_cpp_tmpl_nbinds;

/* Parse a non-type template argument (defined in cpp_parse.c,
 * function-template section); used by the class-template instantiation. */
struct expr *cpp_tmpl_const_arg(struct scope *s);

/* Is template parameter `i` a non-type parameter? (defined in
 * cpp_parse.c); used by the class-template instantiation. */
bool tmpl_param_is_nttp(struct cpp_template *tmpl, int i);

/* Emit __mxx_global_var_init (defined in cpp_gcctor.c); called at the
 * end of the translation unit. */
void cpp_emit_global_ctors(void);

/* Template-instantiation state (defined in cpp_parse.c, function-template
 * section), shared with the future function-template submodules. */
extern const char *g_cpp_tmpl_stack[64];
extern int g_cpp_tmpl_depth;
extern struct type *g_cpp_tmpl_expl_types[16];
extern unsigned long long g_cpp_tmpl_expl_vals[16];
extern bool g_cpp_tmpl_expl_isval[16];
extern int g_cpp_tmpl_expl_n;
extern int g_cpp_pack_stack[64];
extern int g_cpp_pack_depth;

/* Member-method body buffering / replay (defined in cpp_method.c):
 * flush_pending_methods is called after a class body is laid out,
 * cpp_ss_addtok builds a synthesized token stream for inherited ctors. */
void flush_pending_methods(void);
void cpp_ss_addtok(struct token **toks, size_t *n, enum tokenkind k,
    const char *lit, struct location loc);

/* Template data structures shared by the template-instantiation code
 * (cpp_parse.c) and the member-template lowering (cpp_tmpl_member.c).
 * Pure data: the registry/instantiation state (g_cpp_tmpl_stack, packs,
 * etc.) stays in cpp_parse.c. */

/* One template parameter (`T` in `template <typename T> ...`).  The
 * concrete type binding is filled in during instantiation.  A parameter
 * pack (`typename... Args`) collects the remaining instantiation types. */
struct cpp_tmpl_param {
	const char *name;
	bool is_pack;            /* `typename... Args` */
	bool is_nttp;            /* non-type template parameter (`int N` / `auto N`) */
	bool is_dep_nttp;        /* NTTP whose type names an earlier type parameter (`T N`) */
	struct type *nttp_type;  /* fixed NTTP type (NULL for `auto N` / dependent `T N`) */
	/* C++11 default template argument (`template<typename T = int>`): the
	 * buffered tokens after `=`, applied when the parameter is omitted.
	 * NULL when the parameter has no default. */
	struct token *deftoks;
	size_t ndeftoks;
	struct cpp_tmpl_param *next;
};

/* A concrete instantiation of a function template (`max<int>`). */
struct cpp_tmpl_inst {
	char key[128];       /* mangled function name, e.g. "max_i" */
	struct decl *fn;
	struct cpp_tmpl_inst *next;
};

/* A concrete instantiation of a class template (`Foo<int>`): the
 * instantiated class type, under its mangled tag name. */
struct cpp_tmpl_cls_inst {
	char key[128];       /* mangled tag name, e.g. "Foo_i" */
	struct type *t;
	struct cpp_tmpl_cls_inst *next;
};

/* A partial specialization of a class template: `template<typename T>
 * struct Foo<T*> { ... }`.  `params` are this partial's own template
 * parameters, `patargs` is the template-id argument list written against
 * those params (`T`, `*` — the pattern to match concrete args against),
 * and `toks`/`ntoks` is the class body following the class-id (the leading
 * `struct Foo <pattern>` is not included). */
struct cpp_tmpl_partial {
	struct cpp_tmpl_param *params;
	struct token *patargs;
	size_t npatargs;
	struct token *toks;
	size_t ntoks;
	int match_depth;             /* pointer-chain depth of the matched pattern (for partial ordering) */
	struct cpp_tmpl_partial *next;
};

/* A function or class template declaration.  `toks` holds the declaration
 * tokens after the `template <...>` header (function declaration + body,
 * or `class Foo { ... }`); it is replayed with each concrete parameter
 * binding to define the instantiation. */
struct cpp_template {
	const char *name;
	int nparams;
	struct cpp_tmpl_param *params;
	struct token *toks;
	size_t ntoks;
	bool is_class;               /* `template<...> class Foo { ... }` */
	bool is_member;              /* template member function of a class */
	bool is_concept;             /* `template<...> concept Name = expr;` */
	struct type *owner;          /* enclosing class (member templates) */
	struct token *constraint;    /* requires-clause tokens (`requires Expr<T>`) */
	size_t nconstraint;
	struct cpp_tmpl_partial *partials; /* partial specializations (class only) */
	struct cpp_tmpl_inst *insts;
	struct cpp_tmpl_inst **insts_end;
	struct cpp_tmpl_cls_inst *cls_insts;
	struct cpp_tmpl_cls_inst **cls_insts_end;
	struct cpp_template *next;
};

/* Template registry (defined in cpp_parse.c); traversed by the
 * member-template lowering. */
extern struct cpp_template *g_cpp_templates;

/* Dummy callee expression for a pending template call (defined in
 * cpp_parse.c); used by the member-template lowering. */
struct decl *cpp_tmpl_dummy_callee(void);

/* Define a (possibly templated) method of class `class_tag` (defined in
 * cpp_parse.c); used by the member-template lowering. */
void cpp_define_method(struct scope *s, struct type *funct,
                       const char *mname, const char *class_tag,
                       bool is_const, bool is_static, bool is_virtual);

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

/* The pending member call is a template-member call (`obj.get<int>(...)`).
 * Set by cpp_tmpl_member_pend, cleared with the rest of the pending state. */
extern bool g_cpp_member_tmpl;

/* Running counter for synthesized closure classes ``__lambdaN``; shared
 * between the template-instantiation code (cpp_parse.c) and the lambda
 * lowering (cpp_lambda.c). */
extern int g_cpp_lambda_count;

/* Parse a C++ ``class``/``struct`` body (access-control sections,
 * members, base lists).  Defined in cpp_parse.c; the lambda lowering
 * replays a synthesized closure-class definition through it. */
bool cpp_class_decl(struct scope *s);

/* Namespace declarations (defined in cpp_namespace.c): the qualified
 * class name state (for `Class::method` out-of-line definitions), the
 * qualified assembly prefix (for namespace-scope symbol names), the
 * is-namespace-decl peek-ahead, the namespace-decl parser, the
 * visible-namespace registry, and the visible-namespace lookup. */
void cpp_set_qual_class(const char *tag);
const char *cpp_take_qual_class(void);
void cpp_set_qual_ns(struct scope *ns);
struct scope *cpp_take_qual_ns(void);
const char *cpp_ns_asm_prefix(struct scope *s, char *buf, size_t bufsz);
bool cpp_is_namespace_decl(void);
void cpp_namespace_decl(struct scope *s);
void cpp_add_visible_ns(struct scope *ns);

/* `extern "C"` linkage specification (defined in cpp_linkage.c): the
 * peek-ahead parser for `extern "C" { ... }` block form and the
 * `extern "C" int f();` single-declaration form.  Returns true if
 * consumed; false if the token stream did not actually start with
 * `extern "C"` (the `extern` token has been restored in that case). */
bool cpp_linkage_spec(void);

/* C++20 module/import/export declarations (defined in cpp_module.c):
 * module ModuleName; / module :private; / import ModuleName; /
 * import "header"; / export module ...; / export import ...; /
 * export { ... }; / export template ...; / export declaration;. */
void cpp_module_decl(struct scope *s);
void cpp_import_decl(struct scope *s);
void cpp_export_decl(struct scope *s);

/* C++ `extern "C"` linkage context flag (defined in cpp_linkage.c):
 * true when the current declaration is inside an `extern "C"` block
 * or preceded by `extern "C"`.  Used by decl.c to assign LINKC
 * instead of LINKEXTERN. */
extern bool g_cpp_extern_c;

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

/* Virtual-function / vtable construction (defined in cpp_vtable.c);
 * called from the class-decl, member, and ctor paths in cpp_parse.c. */
void cpp_vkey(const char *mname, struct type *funct, bool is_const,
              char *buf, size_t bufsz);
void cpp_compute_vtable(struct type *t);
bool cpp_find_final(struct type *d, const char *key, struct type **owner,
                    struct member **outm);
void cpp_init_vptrs(struct func *f, struct type *t, struct expr *thisp);

/* Function-template declaration / alias machinery (cpp_parse.c +
 * cpp_tmpl_alias.c). */
struct cpp_template *cpp_tmpl_find(const char *name);
void cpp_template_decl(struct scope *s, struct type *owner);
bool cpp_try_abbrev_decl(struct scope *s);
extern struct cpp_template **g_cpp_templates_end;
struct expr *cpp_tmpl_placeholder(const char *name);
void cpp_tmpl_explicit_parse(struct scope *s);
struct expr *cpp_tmpl_const_arg(struct scope *s);
void cpp_constraint_add(struct token **buf, size_t *n, size_t *cap,
                        struct token t);
void cpp_register_alias(const char *name, const char **params, int nparams,
                        struct token *toks, size_t ntoks);
bool cpp_tmpl_alias_lookup(const char *name);
struct type *cpp_tmpl_alias_instantiate(struct scope *s, const char *name);
void cpp_template_alias(struct cpp_template *tmpl);

/* Skip an unselected `if constexpr` / `if consteval` branch at the token
 * level (defined in cpp_constexpr_ctrl.c); called from the constexpr
 * statement interpreter (cpp_constexpr_eval.c) as well. */
void cpp_skip_branch(void);

/* Constexpr interpreter recursion depth (defined in cpp_constexpr.c); used
 * by both the constexpr evaluator (cpp_constexpr_eval.c) and the if-consteval
 * / if-constexpr dispatcher (cpp_constexpr_ctrl.c). */
extern int g_cpp_cexpr_depth;

/* Constexpr aggregate-object mini-memory model (defined in
 * cpp_constexpr_agg.c): record and query constant member-values of a
 * constexpr aggregate (struct/union) so a later member access or return
 * of the object can be folded. */
void cpp_record_cexpr_member(struct decl *obj, unsigned long long offset,
                             unsigned long long val);
void cpp_record_cexpr_aggregate(struct decl *d, struct init *init);
void cpp_record_cexpr_return(struct expr *call, struct decl *obj);
bool cpp_cexpr_member_value(struct decl *obj, unsigned long long offset,
                            unsigned long long *out);
bool cpp_cexpr_ret_member_value(struct expr *call, unsigned long long offset,
                                unsigned long long *out);
bool cpp_copy_cexpr_return(struct expr *call, struct decl *dst);

/* A constexpr function whose body is buffered so a constant-context call
 * (`constexpr int v = sq(5);`, static_assert) can be folded by replaying
 * `{ return <expr> ; }` with the argument values bound.  Struct defined
 * here so both the body-buffering module (cpp_constexpr.c) and the
 * evaluator (cpp_constexpr_eval.c) can traverse the linked list. */
struct cpp_cexpr_fn {
	struct decl *fd;
	char **params;
	struct type **ptypes;
	int nparams;
	struct token *toks;
	size_t ntoks;
	const char **tmpl_params;
	struct type **tmpl_types;
	unsigned long long *tmpl_vals;
	bool *tmpl_isval;
	int ntmpl;
	struct cpp_cexpr_fn *next;
};
extern struct cpp_cexpr_fn *g_cpp_cexpr_fns;

/* Constexpr function body buffering (defined in cpp_constexpr.c): record
 * a constexpr function's `{ ... }` body for compile-time evaluation.
 * Called from decl.c and cpp_method.c. */
void cpp_buffer_constexpr_body(struct decl *d);

/* C23 constexpr-function-definition guard (defined in cpp_constexpr.c):
 * non-zero while a C23 constexpr body is being parsed in decl().  The
 * call-expression parser consults it to reject non-constexpr calls. */
extern int g_cexpr_body;

/* Constexpr function call evaluation (defined in cpp_constexpr_eval.c):
 * fold a constexpr function call to an integer constant; returns NULL
 * when the body is not constant-evaluable. */
struct expr *cpp_constexpr_eval(struct expr *call);

/* Per-class exception thunk record (defined in cpp_newdel_thunk.c);
 * both the throw site (cpp_newdel_exc.c) and the thunk emitter
 * (cpp_newdel_thunk.c) walk the linked list, so the struct is exposed
 * here.  All pointer fields: 0-init leaves a benign NULL record. */
struct cpp_exc_thunk {
	struct type *t;
	struct decl *copy_fn;  /* __meuos_exc_ms_copy_T  (DECLFUNC, LINKEXTERN) */
	struct decl *dtor_fn;  /* __meuos_exc_ms_dtor_T  (DECLFUNC, LINKEXTERN) */
	struct cpp_exc_thunk *next;
};

#endif /* MCC_CPP_PARSE_INTERNAL_H */
