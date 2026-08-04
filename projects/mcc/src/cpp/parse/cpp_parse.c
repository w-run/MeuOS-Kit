/* cpp_parse.c — m++ (C++) parser entry.
 *
 * Stage C.1.3: the C++ parser entry point.  C++ is parsed as a C
 * superset — the C parser (src/parse/) handles C-compatible constructs,
 * and C++-only constructs (class/namespace/template/...) are layered on
 * top here.  The translation-unit loop calls this instead of the C
 * `decl()` loop when the input language is C++ (selected by the m++
 * driver).
 *
 * Currently: `class` declarations with access-control sections
 * (public:/private:/protected:) are handled by cpp_class_decl, which
 * reuses the C type machinery (mktype + addmember + structdecl) while
 * skipping access labels.  Member functions, inheritance, templates,
 * and namespaces are added incrementally in later stages.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "mcc.h"
#include "cpp.h"
#include "cpp_internal.h"
#include "../../c/parse/decl_internal.h"
#include "../../c/parse/expr_internal.h"

/* Pending `this` object for the next member-function call
 * (set by the postfix `.`/`->` lowering, consumed by TLPAREN). */
struct expr *g_cpp_member_this;
/* The class and method name of the pending member call, used by TLPAREN
 * to resolve overloads from the actual argument types. */
struct type *g_cpp_member_class;
const char *g_cpp_member_name;
bool g_cpp_member_const;
bool g_cpp_member_rvalue; /* the pending member object is a temporary */
/* C++17 CTAD: when declspecs sees a class-template name without explicit
 * `<...>` arguments, it records the template name here; decl() completes
 * the deduction from the constructor-call arguments. */
const char *g_cpp_ctad_tmpl;
/* The pending member call is a template-member call (`obj.get<int>(...)`);
 * set by cpp_tmpl_member_pend, cleared with the rest of the pending state. */
bool g_cpp_member_tmpl;
/* C++14 `auto` return type deduction: while parsing the body of a function
 * whose declared return type is `auto`, the first return statement fixes
 * the deduced type and later returns must be compatible.  Set/reset around
 * each function body by the decl / method-body parsers. */
struct type *g_cpp_auto_ret_type;
struct func *g_cpp_auto_ret_func;
/* C++ `extern "C"` linkage context: non-zero when the current declaration
 * is inside an `extern "C"` block or is preceded by `extern "C"`.  Used by
 * getlinkage() in decl.c to assign LINKC instead of LINKEXTERN. */
bool g_cpp_extern_c;
/* postfixexpr nesting depth (set by expr_postfix.c); a pending member
 * call records the depth it was created at so nested argument expressions
 * (which run their own postfixexpr) don't clear it prematurely. */
int g_cpp_postfix_depth;
static int g_cpp_pending_depth;
static bool g_cpp_pending_placeholder;

void
cpp_pending_record_depth(void)
{
	g_cpp_pending_depth = g_cpp_postfix_depth;
	g_cpp_pending_placeholder = false;
}

void
cpp_pending_set_placeholder(void)
{
	g_cpp_pending_placeholder = true;
}

bool
cpp_pending_was_placeholder(void)
{
	return g_cpp_pending_placeholder;
}

void
cpp_pending_clear_at_depth(int depth)
{
	if (g_cpp_pending_depth == depth) {
		g_cpp_member_this = NULL;
		g_cpp_member_class = NULL;
		g_cpp_member_name = NULL;
		g_cpp_member_const = false;
		g_cpp_member_rvalue = false;
		g_cpp_member_tmpl = false;
		g_cpp_pending_placeholder = false;
	}
}

bool
cpp_pending_is_mine(int depth)
{
	return g_cpp_pending_depth == depth;
}

/* deducing-this explicit object parameter (P0847): the declarator calls
 * cpp_explicit_obj_begin before parsing a leading `this X& self` parameter
 * and cpp_explicit_obj_set with the parsed decl; cpp_define_method takes
 * the decl (and clears the pending state) to substitute it for the
 * implicit `this`. */
static struct decl *g_cpp_explicit_obj;
static bool g_cpp_explicit_obj_pending;

void
cpp_explicit_obj_begin(void)
{
	g_cpp_explicit_obj_pending = true;
	g_cpp_explicit_obj = NULL;
}

void
cpp_explicit_obj_set(struct decl *d)
{
	g_cpp_explicit_obj = d;
}

struct decl *
cpp_explicit_obj_take(void)
{
	struct decl *d = g_cpp_explicit_obj_pending ? g_cpp_explicit_obj : NULL;

	g_cpp_explicit_obj_pending = false;
	g_cpp_explicit_obj = NULL;
	return d;
}

/* Record a return expression of an `auto`-returning function body.  The
 * first return fixes the deduced type; later returns must be compatible.
 * Called from the return-statement lowering (stmt.c) when the function's
 * declared return type is the `auto` placeholder. */
void
cpp_auto_return(struct func *f, struct expr *e)
{
	struct type *et;

	et = e ? e->type : NULL;
	if (f != g_cpp_auto_ret_func) {
		g_cpp_auto_ret_func = f;
		g_cpp_auto_ret_type = NULL;
	}
	if (!et || et == &typeauto || et->kind == TYPEVOID)
		error_code(E_DECL, &tok.loc, "unable to deduce the return type of an 'auto' function");
	if (g_cpp_auto_ret_type && !typecompatible(g_cpp_auto_ret_type, et))
		error_code(E_DECL, &tok.loc, "inconsistent deduced return types in 'auto' function");
	g_cpp_auto_ret_type = et;
}

void cpp_define_method(struct scope *s, struct type *funct,
                              const char *mname, const char *class_tag,
                              bool is_const, bool is_static, bool is_virtual);

/* Method-body context: while parsing a member-function body, bare member
 * identifiers (`count`) lower to `(*this).count` via cpp_member_ident,
 * and the implicit `this` parameter is available in scope. */
static struct cpp_method_ctx {
	struct type *class_type; /* enclosing class of the method being parsed */
	struct decl *this_decl;  /* the implicit `this` parameter decl */
	bool active;
} g_cpp_method;

/* Pending qualified-class name from a `Class::method` declarator; consumed
 * by decl()'s DECLFUNC path to route out-of-line method definitions. */
static const char *g_cpp_qual_class;
/* Namespace the qualified class lives in (`ns::Class::method`), or NULL
 * for a plain file-scope `Class::method`. */
static struct scope *g_cpp_qual_ns;

static void cpp_emit_base_ctor(struct func *f);
static void cpp_emit_base_dtor(struct func *f);
static void cpp_parse_init_list(struct func *f, struct scope *fs);
static void cpp_mangle_type(struct type *t, char *buf, size_t bufsz);
static struct member *cpp_method_member(struct type *t, const char *name,
                                        struct type **owner);
static size_t cpp_requires_span_len(struct token **out);
static void flush_pending_methods(void);
static void cpp_emit_global_ctors(void);
static void cpp_emit_global_dtor(struct func *f, struct decl *d);
static void emit_base_ctors_for(struct func *f, struct type *classt,
                                struct expr *thisp);
static void emit_base_dtors_for(struct func *f, struct type *classt,
                                struct expr *thisp);
static bool has_base(struct type *t);
static void cpp_vkey(const char *mname, struct type *funct, bool is_const,
                     char *buf, size_t bufsz);
static void cpp_compute_vtable(struct type *t);
static void cpp_init_vptrs(struct func *f, struct type *t,
                           struct expr *thisp);
static bool cpp_find_final(struct type *d, const char *key,
                           struct type **owner, struct member **outm);
static void cpp_template_decl(struct scope *s, struct type *owner);
static struct decl *cpp_tmpl_find_or_instantiate(struct scope *s,
    const char *name, struct expr *arglist);
static bool cpp_try_abbrev_decl(struct scope *s);
static void cpp_tb(struct token *buf, size_t *n, struct token tmpl,
    enum tokenkind k, const char *name);
/* Set by cpp_define_method when the method just defined is effectively
 * virtual (explicit `virtual` or an override of a base virtual); consumed
 * by structdecl to flag the member. */
/* Set by cpp_define_method when the method just defined is effectively
 * virtual (explicit `virtual` or an override of a base virtual); consumed
 * by structdecl to flag the member. */
bool g_cpp_define_virtual;
/* Set when a method was declared with the `final` specifier. */
bool g_cpp_method_final;

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
static struct cpp_init_item *g_cpp_init_items;
static struct cpp_init_item **g_cpp_init_end = &g_cpp_init_items;

/* Two-phase class parsing: while a class body is being collected, method
 * bodies are buffered (tokens) and replayed after the class layout is
 * known, so a method body may reference members declared later. */
static bool g_cpp_class_parsing;

/* Pending constructor-call arguments collected by declarator() for
 * `Point p(3, 4);` (vexing parse: the args are expressions, not a
 * parameter declaration).  Consumed by decl()'s DECLOBJECT path. */
static struct expr *g_cpp_ctor_args;
static struct expr **g_cpp_ctor_args_end;
static bool g_cpp_ctor_active;

void
cpp_ctor_args_begin(void)
{
	g_cpp_ctor_args = NULL;
	g_cpp_ctor_args_end = &g_cpp_ctor_args;
}

void
cpp_ctor_args_add(struct expr *e)
{
	*g_cpp_ctor_args_end = e;
	g_cpp_ctor_args_end = &e->next;
}

struct expr *
cpp_ctor_args_take(void)
{
	struct expr *r = g_cpp_ctor_args;
	g_cpp_ctor_args = NULL;
	g_cpp_ctor_args_end = &g_cpp_ctor_args;
	return r;
}

void
cpp_ctor_set_active(void)
{
	g_cpp_ctor_active = true;
}

bool
cpp_ctor_is_active(void)
{
	return g_cpp_ctor_active;
}

void
cpp_ctor_clear_active(void)
{
	g_cpp_ctor_active = false;
}
static bool cpp_class_decl(struct scope *s);
static void cpp_namespace_decl(struct scope *s);

/* Namespaces made visible by `using namespace NAME;` directives and by
 * `inline namespace` blocks.  Lookups that fail in the current scope
 * consult these before giving up. */
static struct scope *g_cpp_visible_ns[16];
static int g_cpp_nvisible_ns;
static void cpp_add_visible_ns(struct scope *ns);

static bool cpp_is_namespace_decl(void);
static void cpp_friend_decl(struct scope *s, struct type *classt);
static void cpp_inherit_ctor(struct scope *s, struct type *derived,
                             struct structbuilder *b);
static void cpp_synth_inherited_ctor(struct scope *s, struct type *derived,
    struct structbuilder *b, struct type *base, struct type *bctor);
static void cpp_ss_addtok(struct token **toks, size_t *n, enum tokenkind k,
    const char *lit, struct location loc);

void
cpp_set_qual_class(const char *tag)
{
	g_cpp_qual_class = tag;
}

const char *
cpp_take_qual_class(void)
{
	const char *tag = g_cpp_qual_class;
	g_cpp_qual_class = NULL;
	return tag;
}

void
cpp_set_qual_ns(struct scope *ns)
{
	g_cpp_qual_ns = ns;
}

struct scope *
cpp_take_qual_ns(void)
{
	struct scope *ns = g_cpp_qual_ns;
	g_cpp_qual_ns = NULL;
	return ns;
}

/* Qualified assembly prefix for a declaration inside namespace(s):
 * `Outer_Inner` for a name declared in `namespace Outer { namespace Inner
 * { ... } }`, `Geo` for a single-level `namespace Geo`.  Returns NULL if
 * `s` is the file scope (no enclosing namespace).  The caller uses this
 * to give namespace-scope objects/functions a distinct symbol name so a
 * namespace variable does not collide with a same-named global. */
const char *
cpp_ns_asm_prefix(struct scope *s, char *buf, size_t bufsz)
{
	const char *names[16];
	int n = 0, i;

	(void)bufsz;
	for (; s && s->name; s = s->parent) {
		if (n >= (int)countof(names))
			break;
		names[n++] = s->name;
	}
	if (!n)
		return NULL;
	buf[0] = '\0';
	/* names[] is innermost-first; walk it backwards to build
	 * Outer_Inner (outermost first). */
	for (i = n - 1; i >= 0; i--) {
		if (i != n - 1)
			strcat(buf, "_");
		strcat(buf, names[i]);
	}
	return buf;
}

/* Build the expression for the implicit `this` pointer of the method
 * body currently being parsed (NULL outside a method body).  In a
 * deducing-this method the object parameter is a reference (`this X&
 * self`), so `this` is the address of its referent (&(*self)). */
struct expr *
cpp_this_expr(void)
{
	struct expr *e;
	struct decl *td = g_cpp_method.this_decl;

	if (!g_cpp_method.active || !td)
		return NULL;
	e = mkexpr(EXPRIDENT, td->type, NULL);
	e->qual = td->qual;
	e->lvalue = true;
	e->u.ident.decl = td;
	if (td->type && td->type->kind == TYPEPOINTER && td->type->isref) {
		/* `this` for deducing-this: &(*self), the object pointer */
		struct expr *obj = mkunaryexpr(TMUL, e);
		obj->type = td->type->base;
		obj->lvalue = true;
		e = mkunaryexpr(TBAND, obj);
		e->type = mkpointertype(td->type->base, td->type->qual);
		e->lvalue = true;
	}
	return e;
}

/* Resolve a bare class-member name inside a method body.  Called by the
 * C parser's primary-expression path when scopegetdecl fails; C++
 * semantics say member names shadow nothing here but resolve to the
 * object member of the implicit this.  Data members lower to the
 * lvalue `(*this).name`; function members lower to the mangled free
 * function `Class_method` with the this object pending for the call
 * lowering (TLPAREN) to prepend as the first argument.  Returns NULL
 * when `name` is not a member of the current method's class. */
struct expr *
cpp_member_ident(struct scope *s, const char *name)
{
	struct type *t;
	struct member *m;
	unsigned long long offset = 0;
	struct expr *thise, *base, *e;
	char mname[256];
	struct decl *fd;

	if (!g_cpp_method.active)
		return NULL;
	t = g_cpp_method.class_type;
	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return NULL;
	if (cpp_member_ambiguous(t, name))
		error_code(E_CTYPE, &tok.loc, "request for member '%s' is ambiguous "
		      "(multiple base classes define it)", name);
	m = typemember(t, name, &offset);
	if (!m) {
		/* static data member? `count` -> Class_count */
		char sm[256];
		struct decl *sd;
		snprintf(sm, sizeof sm, "%s_%s",
		    t->u.structunion.tag ? t->u.structunion.tag : "anon", name);
		sd = scopegetdecl(t->scope ? t->scope : s, sm, 1);
		if (sd && sd->kind == DECLOBJECT) {
			e = mkexpr(EXPRIDENT, sd->type, NULL);
			e->qual = sd->qual;
			e->lvalue = true;
			e->u.ident.decl = sd;
			return e;
		}
		return NULL;
	}
	if (m->type && m->type->kind == TYPEFUNC) {
		/* member-function call: mangled free function + this */
		if (!cpp_is_member_function(t, name))
			return NULL;
		bool this_const = g_cpp_method.this_decl &&
		    (g_cpp_method.this_decl->type->qual & QUALCONST);
		if (m->is_virtual) {
			/* virtual call: indirect through this object's vtable */
			struct type *owner;
			struct expr *tp, *adj;
			tp = cpp_this_expr();
			if (!tp)
				return NULL;
			owner = cpp_method_owner(t, name);
			adj = tp;
			if (offset) {
				adj = mkbinaryexpr(&tok.loc, TADD,
				    exprconvert(tp, &typeulong),
				    mkconstexpr(&typeulong, offset));
				adj->type = mkpointertype(owner,
				    this_const ? QUALCONST : QUALNONE);
			}
			e = cpp_make_vcall(adj, owner, m, m->vslot);
			g_cpp_member_this = adj;
			g_cpp_member_class = NULL;
			g_cpp_member_name = NULL;
			g_cpp_member_const = this_const;
			cpp_pending_record_depth();
			return e;
		}
		cpp_mangled_name(t, name, mname, sizeof mname);
		if (this_const)
			strncat(mname, "K", sizeof mname - strlen(mname) - 1);
		fd = scopegetdecl(t->scope ? t->scope : s, mname, 1);
		if (!fd || fd->kind != DECLFUNC) {
			/* overloaded: symbol is argument-encoded; the call lowering
			 * resolves it from the argument types.  Placeholder uses the
			 * member's own function type. */
			e = mkexpr(EXPRIDENT, m->type, NULL);
			e->u.ident.decl = NULL;
			e = decay(e); /* &Class_method */
			g_cpp_member_this = cpp_this_expr();
			g_cpp_member_class = t;
			g_cpp_member_name = name;
			g_cpp_member_const = this_const;
			cpp_pending_record_depth();
			cpp_pending_set_placeholder();
			return e;
		}
		e = mkexpr(EXPRIDENT, fd->type, NULL);
		e->qual = fd->qual;
		e->u.ident.decl = fd;
		e = decay(e); /* &Class_method */
		g_cpp_member_this = cpp_this_expr();
		if (offset) {
			struct expr *tp = mkbinaryexpr(&tok.loc, TADD,
			    exprconvert(g_cpp_member_this, &typeulong),
			    mkconstexpr(&typeulong, offset));
			tp->type = mkpointertype(t, QUALNONE);
			g_cpp_member_this = tp;
		}
		g_cpp_member_class = t;
		g_cpp_member_name = name;
		g_cpp_member_const = this_const;
		cpp_pending_record_depth();
		return e;
	}
	/* data member: (*this).name — mirror postfixexpr()'s TPERIOD
	 * lowering of `obj.member`.  In a const member function the this
	 * pointer is const, so the accessed member is const too. */
	thise = cpp_this_expr();
	if (!thise)
		return NULL;
	{
		enum typequal thisq = g_cpp_method.this_decl
		    ? g_cpp_method.this_decl->type->qual : QUALNONE;
		if (m->is_mutable)
			thisq &= ~QUALCONST;
		base = mkbinaryexpr(&tok.loc, TADD, exprconvert(thise, &typeulong),
		                    mkconstexpr(&typeulong, offset));
		base->type = mkpointertype(m->type, m->qual | thisq);
	}
	e = mkunaryexpr(TMUL, base);
	e->lvalue = true;
	/* C++ reference member: `(*this).name` is a hidden pointer, but the
	 * expression denotes the referent — dereference it, mirroring the
	 * identifier path in expr_primary.c for reference variables.  Lambda
	 * reference captures (`[&x]`) rely on this: the closure member is
	 * declared `T &` and every use inside the operator() body is the
	 * referent lvalue. */
	if (m->type && m->type->isref) {
		e = mkunaryexpr(TMUL, e);
		e->lvalue = true;
	}
	if (m->bits.before || m->bits.after) {
		e = mkexpr(EXPRBITFIELD, e->type, e);
		e->lvalue = true;
		e->u.bitfield.bits = m->bits;
	}
	return e;
}

/* Classify the current token as a C++ keyword, if any.  Wired to the C++
 * lexer's keyword table; the C lexer tokenizes identifiers, and this
 * re-interprets them as C++ keywords for the parser.  Identifier names
 * are recovered via tokenstr() (the C lexer stores identifier text in the
 * tokstr table, not in tok.lit). */
enum cpp_tokenkind
cpp_tok_kind(void)
{
	if (tok.kind >= TIDENT) {
		const char *name = tokenstr(tok.kind);
		return cpp_classify_ident(name, name ? strlen(name) : 0);
	}
	/* C keywords that are also C++ tags: the C lexer gives `struct`/`union`
	 * their own token kinds (TSTRUCT/TUNION) rather than TIDENT, so the
	 * identifier-based classification above never sees them.  Map them to
	 * the C++ kinds so struct/union declarations are dispatched to the
	 * C++ class path like `class`. */
	switch (tok.kind) {
	case TSTRUCT: return CPP_TSTRUCT;
	case TUNION:  return CPP_TUNION;
	/* `inline` is a C keyword (TINLINE, below TIDENT), so the
	 * identifier-based classification above never sees it */
	case TINLINE: return CPP_TINLINE;
	default:      return CPP_TNONE;
	}
}

/* Classify an arbitrary token (not just the current `tok`) as a C++ keyword
 * kind.  Only identifier tokens carry a name to classify; other token kinds
 * map to CPP_TNONE (or the struct/union C++ kinds).  Unlike a bare
 * cpp_classify_ident(tokenstr(t.kind), strlen(...)), this is safe for
 * non-identifier tokens (literals, operators, braces) whose tokenstr() is
 * NULL and would crash strlen().  Used by the requires-expression
 * classifier, where the first token of a requirement span may be anything. */
static enum cpp_tokenkind
cpp_classify_token(struct token t)
{
	if (t.kind >= TIDENT) {
		const char *name = tokenstr(t.kind);
		return cpp_classify_ident(name, name ? strlen(name) : 0);
	}
	switch (t.kind) {
	case TSTRUCT: return CPP_TSTRUCT;
	case TUNION:  return CPP_TUNION;
	case TINLINE: return CPP_TINLINE;
	default:      return CPP_TNONE;
	}
}

/* Is the current token a `struct`/`union` tag declaration with a base-class
 * list (`struct D : A, B`) or a body (`struct S { ... }`)?  Consumes the tag
 * name (and an optional access specifier) to look one token ahead, then
 * restores the token stream so the caller can either hand the whole
 * declaration to cpp_class_decl (base lists and C++ bodies are handled
 * there; the plain C parser rejects the `:`) or fall through to the C
 * struct/union path when the tag is used as a plain type (`struct S s;`). */
static bool
cpp_struct_needs_class_decl(void)
{
	struct token kw, tag, colon, pending;
	enum cpp_tokenkind k = cpp_tok_kind();

	if (k != CPP_TSTRUCT && k != CPP_TUNION)
		return false;
	kw = tok;
	next();
	if (tok.kind < TIDENT) {
		tok = kw; /* not a valid tag decl: restore and bail */
		return false;
	}
	tag = tok;
	next();
	if (tok.kind == TCOLON || tok.kind == TLBRACE) {
		/* base list or body present: restore `struct tag :/{` and report */
		colon = tok;
		tok = kw;
		tokpush(&colon, 1); /* push in reverse so `tag` is consumed first */
		tokpush(&tag, 1);
		return true;
	}
	/* no base list / body: restore `struct tag <next>` and fall through
	 * to the C path */
	pending = tok;
	tok = kw;
	tokpush(&pending, 1);
	tokpush(&tag, 1);
	return false;
}

/* Parse a C++ `class`/`struct`/`union` declaration with access-control
 * sections (public:/private:/protected:).  Reuses the C type machinery
 * (mktype + addmember + structdecl) but skips access-specifier labels,
 * which the C parser does not understand. */
static bool
cpp_class_decl(struct scope *s)
{
	struct type *t;
	struct type *bases[8];
	char *tag;
	struct structbuilder b;
	bool is_class;
	int nbases = 0;

	/* class defaults to private access, struct/union to public (C++). */
	is_class = cpp_tok_kind() == CPP_TCLASS;

	/* class/struct/union keyword consumed here */
	next();
	if (tok.kind < TIDENT)
		error_code(E_SYNTAX, &tok.loc, "expected class name");
	tag = tokenstr(tok.kind);
	next();

	/* base-class list: `class Derived : public A, protected B { ... }`.
	 * Each base becomes an anonymous member (subobject) at its position;
	 * access specifiers are recorded for later access checking. */
	if (tok.kind == TCOLON) {
		next(); /* consume ':' */
		for (;;) {
			enum cpp_tokenkind bk = cpp_tok_kind();
			if (bk == CPP_TPUBLIC || bk == CPP_TPRIVATE || bk == CPP_TPROTECTED)
				next();
			if (tok.kind < TIDENT)
				error_code(E_SYNTAX, &tok.loc, "expected base class name after ':'");
			if (nbases >= (int)countof(bases))
				error_code(E_SYNTAX, &tok.loc, "too many base classes");
			bases[nbases] = scopegettag(s, tokenstr(tok.kind), true);
			if (!bases[nbases] ||
			    (bases[nbases]->kind != TYPESTRUCT && bases[nbases]->kind != TYPEUNION))
				error_code(E_CTYPE, &tok.loc, "'%s' is not a class type", tokenstr(tok.kind));
			++nbases;
			next();
			if (tok.kind == TCOMMA) {
				next();
				continue;
			}
			break;
		}
	}

	/* create or look up the aggregate type */
	t = scopegettag(s, tag, tok.kind != TLBRACE && tok.kind != TSEMICOLON);
	if (t) {
		if (t->kind != TYPESTRUCT && t->kind != TYPEUNION)
			error_code(E_REDEF, &tok.loc, "redeclaration of tag '%s' with different kind", tag);
	} else {
		t = mktype(TYPESTRUCT, 0);
		t->size = 0;
		t->align = 0;
		t->u.structunion.tag = tag;
		t->u.structunion.members = NULL;
		t->incomplete = true;
		scopeputtag(s, tag, t);
	}

	if (tok.kind != TLBRACE)
		return true; /* forward declaration */
	if (!t->incomplete)
		error_tok_code(E_REDEF, &tok, "redefinition of class '%s'", tag);
	{
		int bi;
		for (bi = 0; bi < nbases; ++bi)
			if (bases[bi]->incomplete)
				error_code(E_INCOMPLETE, &tok.loc, "base class '%s' has incomplete type",
				      bases[bi]->u.structunion.tag);
	}
	t->scope = s; /* member symbols are registered here */
	next(); /* consume '{' */
	{
		/* Two-phase buffering nests: an inner class body buffers its own
		 * methods, and the outer class's parsing flag must be restored. */
		bool saved_parsing = g_cpp_class_parsing;
		g_cpp_class_parsing = true;

		b.type = t;
		b.last = &t->u.structunion.members;
		b.bits = 0;
		b.pack = false;
		b.access = is_class ? ACC_PRIVATE : ACC_PUBLIC;
		b.member_mutable = false;
		b.member_virtual = false;
		b.member_const = false;

		/* Each base-class subobject is registered as an anonymous member
		 * so typemember()'s recursive search and the layout computation
		 * see them like ordinary members (first base at offset 0). */
		{
			int bi;
			for (bi = 0; bi < nbases; ++bi) {
				struct qualtype bq;
				bq.type = bases[bi];
				bq.qual = QUALNONE;
				bq.expr = NULL;
				addmember(&b, bq, NULL, 0, -1);
			}
		}

		for (;;) {
			/* access-control labels: public: private: protected: */
			enum cpp_tokenkind k = cpp_tok_kind();
			if (k == CPP_TPUBLIC || k == CPP_TPRIVATE || k == CPP_TPROTECTED) {
				b.access = k == CPP_TPUBLIC ? ACC_PUBLIC
				         : k == CPP_TPROTECTED ? ACC_PROTECTED
				         : ACC_PRIVATE;
				next(); /* consume the keyword */
				if (tok.kind == TCOLON)
					next(); /* consume ':' */
				continue;
			}
			if (tok.kind == TCOLON) {
				/* stray colon */
				next();
				continue;
			}
			if (k == CPP_TFRIEND) {
				/* friend declaration: a free function/operator defined
				 * (or declared) here that may access this class's
				 * private members, or a friend class */
				cpp_friend_decl(s, t);
				continue;
			}
			if (k == CPP_TUSING) {
				/* C++11 inherited constructors: `using Base::Base;` —
				 * synthesize derived constructors forwarding to the
				 * base ones */
				cpp_inherit_ctor(s, t, &b);
				continue;
			}
			if (k == CPP_TCLASS || k == CPP_TSTRUCT || k == CPP_TUNION) {
				/* nested class/struct definition; a struct/union only via
				 * cpp_class_decl when it has a body or base-class list,
				 * otherwise it is a plain member struct (C path) */
				if (k == CPP_TCLASS || cpp_struct_needs_class_decl())
					cpp_class_decl(s);
				else
					structdecl(s, &b);
				continue;
			}
			if (k == CPP_TTEMPLATE) {
				/* template member function: `template <typename T>
				 * T get() {...}`.  Registered as a member template of
				 * the enclosing class `t`; instantiated on first use
				 * at a call site (`obj.get<int>()`). */
				cpp_template_decl(s, t);
				continue;
			}
			if (tok.kind == TRBRACE)
				break;
			/* C++20 [[no_unique_address]]: capture the attribute before
			 * structdecl so it can be propagated to the member layout. */
			{
				struct attr _ma = {0};
				attr(&_ma, ATTRNOUNIQUEADDRESS);
				b.member_no_unique_address = (_ma.kind & ATTRNOUNIQUEADDRESS) != 0;
			}
			structdecl(s, &b);
		}
		next(); /* consume '}' */
		g_cpp_class_parsing = saved_parsing;
	}

	/* Finalize the layout BEFORE parsing method bodies: method bodies may
	 * construct objects of this class (temporary `Vec(...)`) or use
	 * members, so the type must be complete and sized. */
	if (t->align < 0)
		t->align = 0;
	if (t->size)
		t->size = ALIGNUP(t->size, t->align);
	else
		t->size = 1; /* C++ empty class is 1 byte (distinct addresses) */
	if (!t->align)
		t->align = 1;
	t->incomplete = false;

	/* C++ virtual dispatch (C.2.5): compute the vtable slot layout, insert
	 * the hidden vptr member if the class needs its own (no polymorphic
	 * primary base), and record the class for vtable emission. */
	cpp_compute_vtable(t);

	/* Phase two: layout is fixed, parse the buffered method bodies. */
	flush_pending_methods();

	/* trailing ';' after the class body */
	if (tok.kind == TSEMICOLON)
		next();
	return true;
}

/* friend declaration inside a class body: `friend int f() {...}` /
 * `friend int operator==(...) {...}` (a file-scope free function granted
 * access to this class's private members) or `friend class B;` (every
 * method of B may access the private members).  The function form is
 * parsed with the ordinary declaration machinery — tok is positioned
 * after `friend` — with the method context pointed at the befriending
 * class (no `this`), so the access checker (cpp_member_accessible /
 * cpp_same_class_context) accepts private-member accesses from the
 * function's body. */
static void
cpp_friend_decl(struct scope *s, struct type *classt)
{
	next(); /* consume 'friend' */
	enum cpp_tokenkind k = cpp_tok_kind();
	if (k == CPP_TCLASS || k == CPP_TSTRUCT || k == CPP_TUNION) {
		/* friend class B; */
		struct cpp_friend *fr;
		struct type *ft;
		next(); /* consume class/struct/union */
		if (tok.kind < TIDENT)
			error_code(E_SYNTAX, &tok.loc, "expected class name after 'friend'");
		ft = scopegettag(s, tokenstr(tok.kind), false);
		if (ft && ft->kind != TYPESTRUCT && ft->kind != TYPEUNION)
			error_code(E_CTYPE, &tok.loc, "'%s' is not a class type",
			    tokenstr(tok.kind));
		if (!ft) {
			/* `friend class B;` also introduces B as an incomplete
			 * class in the enclosing scope (a later definition
			 * completes the same type object) */
			ft = mktype(TYPESTRUCT, 0);
			ft->size = 0;
			ft->align = 0;
			ft->u.structunion.tag = (char *)tokenstr(tok.kind);
			ft->u.structunion.members = NULL;
			ft->incomplete = true;
			scopeputtag(s, ft->u.structunion.tag, ft);
		}
		for (fr = classt->u.structunion.friends; fr; fr = fr->next)
			if (fr->cls == ft)
				break;
		if (!fr) {
			fr = xmalloc(sizeof *fr);
			fr->cls = ft;
			fr->next = classt->u.structunion.friends;
			classt->u.structunion.friends = fr;
		}
		next(); /* consume the class name */
		expect(TSEMICOLON, "after friend class declaration");
		return;
	}
	{
		struct cpp_method_ctx saved = g_cpp_method;
		g_cpp_method.class_type = classt;
		g_cpp_method.this_decl = NULL;
		g_cpp_method.active = true;
		decl(s, NULL);
		g_cpp_method = saved;
	}
}

/* C++11 inherited constructors: `using Base::Base;` inside a derived
 * class synthesizes one derived-class constructor per base-class
 * constructor signature, each forwarding every parameter to the base
 * constructor via a `: Base(args) { }` body (so the normal
 * init-list/base-ctor machinery emits the base call).  Other `using`
 * forms inside a class body are not supported yet and are consumed
 * silently. */
static void
cpp_inherit_ctor(struct scope *s, struct type *derived,
                 struct structbuilder *b)
{
	extern void tokpush(struct token *, size_t);
	extern void next(void);

	struct type *base;
	struct member *m;
	const char *bname, *mname;

	next(); /* consume 'using' */
	if (tok.kind < TIDENT)
		error_code(E_SYNTAX, &tok.loc,
		    "expected base class name after 'using'");
	bname = tokenstr(tok.kind);
	next();
	if (tok.kind != TCOLONCOLON) {
		error_code(E_SYNTAX, &tok.loc,
		    "expected '::' after base class name in using declaration");
		return;
	}
	next();
	if (tok.kind < TIDENT)
		error_code(E_SYNTAX, &tok.loc,
		    "expected member name after '::'");
	mname = tokenstr(tok.kind);
	next();
	expect(TSEMICOLON, "after using declaration");

	/* only constructor inheritance (`using Base::Base`) is supported */
	if (strcmp(bname, mname) != 0)
		return;

	base = scopegettag(s, bname, false);
	if (!base || (base->kind != TYPESTRUCT && base->kind != TYPEUNION))
		error_code(E_CTYPE, &tok.loc, "'%s' is not a class type", bname);

	/* the base must be a direct base (an anonymous subobject member) */
	{
		bool direct = false;
		for (m = derived->u.structunion.members; m; m = m->next)
			if (!m->name && m->type == base) {
				direct = true;
				break;
			}
		if (!direct)
			error_code(E_CTYPE, &tok.loc,
			    "'%s' is not a direct base of '%s'", bname,
			    derived->u.structunion.tag);
	}

	/* one synthesized ctor per base ctor signature */
	for (m = base->u.structunion.members; m; m = m->next)
		if (m->name && strcmp(m->name, bname) == 0 &&
		    m->type && m->type->kind == TYPEFUNC)
			cpp_synth_inherited_ctor(s, derived, b, base, m->type);
}

/* Synthesize one inherited constructor `Derived(params) : Base(params) {}`
 * from the base constructor type `bctor`.  The parameter decls are copied
 * (unnamed parameters get internal names so the forwarding init list can
 * reference them) and the `: Base(a0, ...) { }` token stream is replayed
 * through cpp_define_method, which buffers it like any in-class method
 * body (two-phase). */
static void
cpp_synth_inherited_ctor(struct scope *s, struct type *derived,
    struct structbuilder *b, struct type *base, struct type *bctor)
{
	extern void cpp_define_method(struct scope *, struct type *,
	    const char *, const char *, bool, bool, bool);
	extern void tokpush(struct token *, size_t);
	extern void next(void);

	struct type *ct;
	struct decl *sp, *pd, **pend;
	struct token *toks = NULL;
	size_t n = 0;
	int argno = 0;
	bool first = true;
	struct location loc = tok.loc;

	/* derived-class ctor type: same parameter list as the base ctor,
	 * void return */
	ct = mktype(TYPEFUNC, 0);
	ct->qual = QUALNONE;
	ct->base = &typevoid;
	ct->u.func.isvararg = bctor->u.func.isvararg;
	ct->u.func.params = NULL;
	ct->u.func.nparam = 0;
	pend = &ct->u.func.params;

	/* synthesize `: Base(a0, a1, ...) { }` — the init list forwards
	 * every parameter to the base ctor; the body is empty */
	cpp_ss_addtok(&toks, &n, TCOLON, NULL, loc);
	cpp_ss_addtok(&toks, &n, TIDENT, (char *)base->u.structunion.tag, loc);
	cpp_ss_addtok(&toks, &n, TLPAREN, NULL, loc);
	for (sp = bctor->u.func.params; sp; sp = sp->next) {
		char namebuf[32];
		const char *pname = sp->name;
		if (!pname) {
			snprintf(namebuf, sizeof namebuf, "__mcc_inh%d", argno);
			pname = xmalloc(strlen(namebuf) + 1);
			strcpy((char *)pname, namebuf);
		}
		pd = mkdecl((char *)pname, DECLOBJECT, sp->type, sp->qual,
		            LINKNONE);
		pd->u.obj.storage = SDAUTO;
		*pend = pd;
		pend = &pd->next;
		++ct->u.func.nparam;
		if (!first)
			cpp_ss_addtok(&toks, &n, TCOMMA, NULL, loc);
		first = false;
		cpp_ss_addtok(&toks, &n, TIDENT, (char *)pname, loc);
		++argno;
	}
	cpp_ss_addtok(&toks, &n, TRPAREN, NULL, loc);
	cpp_ss_addtok(&toks, &n, TLBRACE, NULL, loc);
	cpp_ss_addtok(&toks, &n, TRBRACE, NULL, loc);

	/* replay: keep the following class-body token, push the synthesized
	 * init list + body in front, and let cpp_define_method buffer it
	 * (the class layout is not fixed yet).  tokpush keeps the token
	 * pointer, so the saved token is heap-copied (like struct_decl's
	 * '&' restore) */
	{
		struct token *cur = xmalloc(sizeof *cur);
		*cur = tok;
		tokpush(cur, 1);
		tokpush(toks, n);
		next(); /* position at the synthesized ':' */
	}
	cpp_define_method(s, ct, derived->u.structunion.tag,
	    derived->u.structunion.tag, false, false, false);
	addmember(b, (struct qualtype){ct, QUALNONE, NULL},
	    derived->u.structunion.tag, 0, -1);
	free(toks);
}

/* Parse a C++ `namespace NAME { ... }` block.  Inner declarations are
 * registered in a fresh scope named NAME; the namespace itself is
 * registered in the enclosing scope as a DECLNAMESPACE so `NAME::symbol`
 * can be resolved (single-level for now).  The namespace scope outlives
 * parsing (never delscope'd) so qualified lookups keep working. */
/* Is the current token the start of a namespace declaration, possibly
 * prefixed by the C++11 `inline` keyword (`inline namespace NAME {`)?
 * Peeks one token ahead for the `inline` case and restores the stream. */
static bool
cpp_is_namespace_decl(void)
{
	enum cpp_tokenkind k = cpp_tok_kind();

	if (k == CPP_TNAMESPACE)
		return true;
	if (k == CPP_TINLINE) {
		struct token save = tok;
		struct token peek;
		struct token *tp;
		next();
		peek = tok;
		tok = save;
		/* tokpush stores the token pointer, so the peeked token must
		 * outlive this frame (heap copy, like struct_decl's '&' restore) */
		tp = xmalloc(sizeof *tp);
		*tp = peek;
		tokpush(tp, 1);
		return cpp_classify_token(peek) == CPP_TNAMESPACE;
	}
	return false;
}

static void
cpp_namespace_decl(struct scope *s)
{
	struct scope *ns;
	struct decl *nd;
	const char *name;
	bool is_inline = false;

	if (cpp_tok_kind() == CPP_TINLINE) {
		is_inline = true;
		next(); /* consume 'inline' */
	}
	next(); /* consume 'namespace' */
	if (tok.kind < TIDENT)
		error_code(E_SYNTAX, &tok.loc, "expected namespace name");
	name = tokenstr(tok.kind);
	next();
	expect(TLBRACE, "after namespace name");

	ns = mkscope(s);
	ns->name = name;
	nd = mkdecl((char *)name, DECLNAMESPACE, NULL, QUALNONE, LINKNONE);
	nd->u.ns = ns;
	scopeputdecl(s, nd);

	/* inline namespace (C++11): its members are visible in the enclosing
	 * scope, like a `using namespace` directive — but only when the
	 * enclosing scope is itself visible to name lookup (the file scope
	 * or a namespace already brought in by a using-directive), so a
	 * plain `namespace A { inline namespace B { ... } }` does not leak
	 * B's members into the file scope. */
	if (is_inline) {
		extern struct scope filescope;
		bool parent_visible = s == &filescope;
		int i;
		for (i = 0; !parent_visible && i < g_cpp_nvisible_ns; i++)
			if (g_cpp_visible_ns[i] == s)
				parent_visible = true;
		if (parent_visible)
			cpp_add_visible_ns(ns);
	}

	while (tok.kind != TRBRACE && tok.kind != TEOF) {
		enum cpp_tokenkind k = cpp_tok_kind();
		if (cpp_is_namespace_decl()) {
			cpp_namespace_decl(ns);
			continue;
		}
		if (k == CPP_TCLASS || k == CPP_TSTRUCT || k == CPP_TUNION) {
			if (k == CPP_TCLASS || cpp_struct_needs_class_decl())
				cpp_class_decl(ns);
			else
				decl(ns, NULL);
			continue;
		}
		if (tok.kind == TSEMICOLON) {
			next();
			continue;
		}
		if (!decl(ns, NULL))
			error_code(E_SYNTAX, &tok.loc, "expected declaration in namespace body");
	}
	next(); /* consume '}' */
	/* deliberately keep ns alive for later NAME::name lookups */
}

static void
cpp_add_visible_ns(struct scope *ns)
{
	if (g_cpp_nvisible_ns >= (int)countof(g_cpp_visible_ns))
		return;
	g_cpp_visible_ns[g_cpp_nvisible_ns++] = ns;
}

/* Resolve `name` in the visible (`using namespace`) namespaces. */
struct decl *
cpp_lookup_visible(struct scope *s, const char *name)
{
	int i;

	(void)s;
	for (i = 0; i < g_cpp_nvisible_ns; i++) {
		struct decl *d = scopegetdecl(g_cpp_visible_ns[i], name, 1);
		if (d)
			return d;
	}
	return NULL;
}

/* `using namespace NAME;`, `using NAME::member;`, or a C++11 alias
 * declaration `using Name = Type;`.  Non-static so the shared C
 * declaration parser can dispatch block-scope `using` (and thus for/if
 * init-statement alias declarations, P2360) to the C++ frontend. */
void
cpp_using_decl(struct scope *s)
{
	next(); /* consume 'using' */
	/* C++20 `using enum E;` — bring all enumerators of E into
	 * the current scope.  `enum` is a C keyword (TENUM), so the
	 * C++ lexer token is used directly. */
	if (tok.kind == TENUM) {
		const char *etag;
		struct type *et;
		size_t i;
		next(); /* consume 'enum' */
		if (tok.kind < TIDENT)
			error_code(E_SYNTAX, &tok.loc, "expected enum name after 'using enum'");
		etag = tokenstr(tok.kind);
		et = scopegettag(s, etag, 1);
		if (!et || et->kind != TYPEENUM)
			error_code(E_CTYPE, &tok.loc, "'%s' is not an enum type", etag);
		next(); /* consume enum name */
		expect(TSEMICOLON, "after using enum declaration");
		if (et->incomplete)
			error_code(E_INCOMPLETE, &tok.loc, "cannot use incomplete enum type '%s'", etag);
		/* Bring all enumerators (DECLCONST decls) of this enum type
		 * into the current scope.  Walk the scope chain looking for
		 * DECLCONST decls whose type matches the enum.  Since the map
		 * is a hash table, we iterate over the current scope's decl
		 * map directly. */
		for (i = 0; i < s->decls.cap; i++) {
			if (s->decls.keys[i].str) {
				struct decl *d = s->decls.vals[i].p;
				if (d && d->kind == DECLCONST && d->type == et) {
					/* Re-insert into the same scope (already
					 * visible, but ensures lookup works). */
					scopeputdecl(s, d);
				}
			}
		}
		return;
	}
	if (cpp_tok_kind() == CPP_TNAMESPACE) {
		struct decl *nsd;
		next(); /* consume 'namespace' */
		if (tok.kind < TIDENT)
			error_code(E_SYNTAX, &tok.loc, "expected namespace name after 'using namespace'");
		nsd = scopegetdecl(s, tokenstr(tok.kind), 1);
		if (!nsd || nsd->kind != DECLNAMESPACE)
			error_code(E_CTYPE, &tok.loc, "'%s' is not a namespace", tokenstr(tok.kind));
		cpp_add_visible_ns(nsd->u.ns);
		next();
		expect(TSEMICOLON, "after using directive");
		return;
	}
	/* using NAME::member; or using Name = Type; */
	{
		struct decl *nsd;
		const char *nm;
		if (tok.kind < TIDENT)
			error_code(E_SYNTAX, &tok.loc, "expected namespace name in using declaration");
		nm = tokenstr(tok.kind);
		nsd = scopegetdecl(s, nm, 1);
		next();
		if (tok.kind == TASSIGN) {
			/* C++11 alias declaration: `using Name = Type;` */
			struct type *at;
			next(); /* consume '=' */
			at = typename(s, NULL, NULL);
			if (!at)
				error_code(E_SYNTAX, &tok.loc, "expected type name in alias declaration");
			expect(TSEMICOLON, "after alias declaration");
			scopeputdecl(s, mkdecl((char *)nm, DECLTYPE, at, QUALNONE, LINKNONE));
			return;
		}
		expect(TCOLONCOLON, "after namespace name in using declaration");
		if (tok.kind < TIDENT)
			error_code(E_SYNTAX, &tok.loc, "expected member name after '::'");
		if (!nsd || nsd->kind != DECLNAMESPACE)
			error_code(E_CTYPE, &tok.loc, "'%s' is not a namespace", nsd ? nsd->name : "?");
		{
			struct decl *md = scopegetdecl(nsd->u.ns, tokenstr(tok.kind), 1);
			if (!md)
				error_code(E_CTYPE, &tok.loc, "no member named '%s' in namespace '%s'",
				      tokenstr(tok.kind), nsd->name);
			scopeputdecl(s, md);
		}
		next();
		expect(TSEMICOLON, "after using declaration");
	}
}

/* C++20 module declaration: `module ModuleName;` or `module :private;`.
 * Only syntax parsing — no semantic module loading. */
void
cpp_module_decl(struct scope *s)
{
	next(); /* consume 'module' */

	if (tok.kind == TCOLON) {
		/* `module :private;` — private module fragment */
		next(); /* consume ':' */
		if (cpp_tok_kind() != CPP_TPRIVATE)
			error_code(E_SYNTAX, &tok.loc, "expected 'private' after 'module :'");
		next(); /* consume 'private' */
		expect(TSEMICOLON, "after module :private");
		return;
	}

	/* Parse module name: identifier (. identifier)* */
	if (tok.kind >= TIDENT) {
		for (;;) {
			next(); /* consume identifier */
			if (tok.kind == TPERIOD) {
				next(); /* consume '.' */
				if (tok.kind < TIDENT)
					error_code(E_SYNTAX, &tok.loc, "expected identifier after '.' in module name");
			} else {
				break;
			}
		}
	}
	expect(TSEMICOLON, "after module declaration");
}

/* C++20 import declaration: `import ModuleName;` or `import "header";`.
 * Only syntax parsing — no semantic module loading. */
void
cpp_import_decl(struct scope *s)
{
	next(); /* consume 'import' */

	/* C++23 header import: `import "header";` */
	if (tok.kind == TSTRINGLIT) {
		next(); /* consume string literal */
		expect(TSEMICOLON, "after header import");
		return;
	}

	/* Parse module name: identifier (. identifier)* */
	if (tok.kind >= TIDENT) {
		for (;;) {
			next(); /* consume identifier */
			if (tok.kind == TPERIOD) {
				next(); /* consume '.' */
				if (tok.kind < TIDENT)
					error_code(E_SYNTAX, &tok.loc, "expected identifier after '.' in module name");
			} else {
				break;
			}
		}
	}
	expect(TSEMICOLON, "after import declaration");
}

/* C++20 export declaration: `export module ...`, `export import ...`,
 * `export { ... }`, `export declaration`, or `export template ...`.
 * Only syntax parsing — no semantic export tracking. */
void
cpp_export_decl(struct scope *s)
{
	next(); /* consume 'export' */

	enum cpp_tokenkind k = cpp_tok_kind();
	if (k == CPP_TMODULE) {
		/* `export module ModuleName;` — module interface declaration */
		cpp_module_decl(s);
	} else if (k == CPP_TIMPORT) {
		/* `export import ModuleName;` — re-export an imported module */
		cpp_import_decl(s);
	} else if (tok.kind == TLBRACE) {
		/* `export { ... }` — export block */
		next(); /* consume '{' */
		while (tok.kind != TRBRACE && tok.kind != TEOF) {
			enum cpp_tokenkind k2 = cpp_tok_kind();
			if (k2 == CPP_TEXPORT)
				cpp_export_decl(s);
			else if (k2 == CPP_TIMPORT)
				cpp_import_decl(s);
			else if (k2 == CPP_TMODULE)
				cpp_module_decl(s);
			else if (k2 == CPP_TUSING)
				cpp_using_decl(s);
			else if (k2 == CPP_TTEMPLATE)
				cpp_template_decl(s, NULL);
			else if (k2 == CPP_TCLASS || k2 == CPP_TSTRUCT || k2 == CPP_TUNION)
				cpp_class_decl(s);
			else if (cpp_is_namespace_decl())
				cpp_namespace_decl(s);
			else
				decl(s, NULL);
		}
		if (tok.kind == TRBRACE)
			next(); /* consume '}' */
	} else if (k == CPP_TTEMPLATE) {
		/* `export template <...> ...` */
		cpp_template_decl(s, NULL);
	} else {
		/* `export declaration` — parse the declaration normally */
		decl(s, NULL);
	}
}

/* Parse a C++ translation unit: top-level declaration loop.
 * C++ grammar is layered over the C parser; `class` declarations with
 * access control are handled here (cpp_class_decl), and C-compatible
 * declarations fall through to the shared C parser. */
void
cpp_parse_translation_unit(void)
{
	extern struct scope filescope;
	extern void emittentativedefns(void);
	extern int g_lang;

	while (tok.kind != TEOF) {
		/* Multi-error collection (--error-json): arm a recovery jump
		 * buffer so error() longjmps back after each collected error
		 * and parsing resumes at the next top-level item. */
		if (g_error_json) {
			if (setjmp(g_err_recovery) != 0) {
				g_err_recovery_set = 0;
				err_sync();
				continue;
			}
			g_err_recovery_set = 1;
		}
		/* C++ class/struct/union with access control */
		enum cpp_tokenkind k = cpp_tok_kind();
		if (k == CPP_TCLASS || k == CPP_TSTRUCT || k == CPP_TUNION) {
			/* `class C ...` always goes to cpp_class_decl; a struct/union
			 * goes there too when it has a body or base-class list
			 * (`struct D : A {...}`); a bare tag use (`struct S s;`) stays
			 * on the C path. */
			if (k == CPP_TCLASS || cpp_struct_needs_class_decl())
				cpp_class_decl(&filescope);
			else
				goto c_decl;
			g_err_recovery_set = 0;
			continue;
		}
		if (cpp_is_namespace_decl()) {
			cpp_namespace_decl(&filescope);
			g_err_recovery_set = 0;
			continue;
		}
		if (k == CPP_TUSING) {
			cpp_using_decl(&filescope);
			g_err_recovery_set = 0;
			continue;
		}
		if (k == CPP_TTEMPLATE) {
			cpp_template_decl(&filescope, NULL);
			g_err_recovery_set = 0;
			continue;
		}
		if (k == CPP_TEXPORT) {
			cpp_export_decl(&filescope);
			g_err_recovery_set = 0;
			continue;
		}
		if (k == CPP_TIMPORT) {
			cpp_import_decl(&filescope);
			g_err_recovery_set = 0;
			continue;
		}
		if (k == CPP_TMODULE) {
			cpp_module_decl(&filescope);
			g_err_recovery_set = 0;
			continue;
		}

	/* C++ `extern "C"` linkage specification: `extern "C" { ... }` block
	 * form or `extern "C" int f();` single-declaration form.  Intercept
	 * before the C parser's decl() sees `extern` as a storage class.
	 *
	 * Peek-ahead: save the extern token, consume the next token, check
	 * for "C".  If it IS extern "C" handle it; if not, use the same
	 * pushback pattern as `consume()` in pp.c (copy the peeked token
	 * to a local, restore the original, then ctxpush the copy). */
	if (tok.kind == TEXTERN && g_lang == 1) {
		struct token save = tok;
		struct token peek;
		next();
		peek = tok;
		/* The C lexer stores string literal content INCLUDING the
		 * surrounding quotes in tok.lit, so we compare against "\"C\""
		 * (the literal text `"C"` with quote characters). */
		if (peek.kind == TSTRINGLIT && peek.lit &&
		    peek.lit[0] == '"' && peek.lit[1] == 'C' && peek.lit[2] == '"' && peek.lit[3] == '\0') {
			/* extern "C": consume the "C" token (tok currently points to
			 * it because next() advanced past extern). */
			next(); /* consume "C" string literal */
			char *saved = g_cpp_extern_c ? strdup("nested") : NULL;
			if (tok.kind == TLBRACE) {
				/* `extern "C" { ... }` — parse all declarations inside
				 * the block with C linkage, then restore. */
				next(); /* consume '{' */
				g_cpp_extern_c = true;
				while (tok.kind != TRBRACE && tok.kind != TEOF) {
					enum cpp_tokenkind k2 = cpp_tok_kind();
					if (k2 == CPP_TCLASS || k2 == CPP_TSTRUCT ||
					    k2 == CPP_TUNION) {
						if (k2 == CPP_TCLASS || cpp_struct_needs_class_decl())
							cpp_class_decl(&filescope);
						else
							decl(&filescope, NULL);
					} else if (cpp_is_namespace_decl()) {
						cpp_namespace_decl(&filescope);
					} else if (k2 == CPP_TUSING) {
						cpp_using_decl(&filescope);
					} else if (k2 == CPP_TTEMPLATE) {
						cpp_template_decl(&filescope, NULL);
					} else {
						decl(&filescope, NULL);
					}
				}
				if (tok.kind == TRBRACE)
					next(); /* consume '}' */
				g_cpp_extern_c = saved ? true : false;
				if (saved)
					free(saved);
			} else {
				/* `extern "C" int f();` — single declaration with C linkage.
				 * Set the flag, parse the declaration, then restore. */
				g_cpp_extern_c = true;
				decl(&filescope, NULL);
				g_cpp_extern_c = saved ? true : false;
				if (saved)
					free(saved);
			}
			g_err_recovery_set = 0;
			continue;
		}
		/* not `extern "C"` — restore the extern token and push the
		 * peeked token back so the normal C parser sees `extern int ...`. */
		tok = save;
		tokpush(&peek, 1);
	}

	/* C++20 abbreviated function templates: `void f(Integral auto x)`
	 * / `auto g(Integral auto x) -> int` lower to an equivalent
	 * `template<typename __T0> requires Integral<__T0> ...` declaration
	 * (handled when the declaration has a constrained/unconstrained
	 * `auto` parameter; otherwise the stream is left untouched). */
	if (cpp_try_abbrev_decl(&filescope)) {
		g_err_recovery_set = 0;
		continue;
	}

	c_decl:
		if (!decl(&filescope, NULL)) {
			if (tok.kind == TSEMICOLON)
				error_code(E_SYNTAX, &tok.loc, "unexpected ';' at top-level");
			error_code(E_SYNTAX, &tok.loc, "expected declaration or function definition");
		}
		g_err_recovery_set = 0;
	}
	emittentativedefns();
	cpp_emit_global_ctors();
	cpp_emit_vtables();
}

/* --- member function lowering (C.2.3) -------------------------------- */

/* Two-phase class parsing: while a class body is being collected, method
 * bodies are buffered (tokens) and replayed after the class layout is
 * known, so a method body may reference members declared later in the
 * class. */
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
	 * bindings (type params and NTTP constants) in effect when this body
	 * was buffered.  A deferred body is parsed long after the
	 * instantiation returned, by which time a later instantiation of the
	 * same template (`C<int,5>` then `C<int,7>`) has overwritten the
	 * file-scope bindings — so they are re-installed before the replay. */
	struct decl *binds[16];
	int nbinds;
	struct cpp_pending_method *next;
};

static struct cpp_pending_method *g_cpp_pending_methods;
static struct cpp_pending_method **g_cpp_pending_methods_end =
    &g_cpp_pending_methods;

/* Class-template instantiation: while the buffered class definition is
 * replayed, member-function bodies are queued but NOT parsed — an unused
 * member whose body is ill-formed for the concrete type (e.g.
 * `void bad() { val.nonexistent(); }` inside `Box<int>`) must not break
 * the whole instantiation (D2).  Such bodies go into the deferred table
 * and are parsed lazily by cpp_ensure_method_defined when the member is
 * actually called. */
static bool g_cpp_tmpl_instantiating;
static struct cpp_pending_method *g_cpp_deferred_methods;
static struct cpp_pending_method **g_cpp_deferred_end =
    &g_cpp_deferred_methods;

/* Template parameter bindings of the instantiation currently being
 * replayed (see cpp_pending_method::binds).  Valid only while
 * g_cpp_tmpl_instantiating is set. */
static struct decl *g_cpp_tmpl_binds[16];
static int g_cpp_tmpl_nbinds;

/* Global class-typed objects with user constructors; their construction
 * calls are collected and emitted into __mxx_global_var_init (wired to
 * the .init_array section so the runtime runs them before main). */
struct cpp_global_ctor {
	struct decl *d;
	struct expr *args;   /* ctor call arguments (may be NULL) */
	struct cpp_global_ctor *next;
};
static struct cpp_global_ctor *g_cpp_global_ctors;
static struct cpp_global_ctor **g_cpp_global_ctors_end =
    &g_cpp_global_ctors;

void
cpp_record_global_ctor(struct decl *d, struct expr *args)
{
	struct cpp_global_ctor *g;
	const char *tag;

	if (!d || !d->type || (d->type->kind != TYPESTRUCT && d->type->kind != TYPEUNION))
		return;
	tag = d->type->u.structunion.tag;
	if (!tag || !cpp_has_ctor(d->type, tag))
		return;
	g = xmalloc(sizeof *g);
	g->d = d;
	g->args = args;
	g->next = NULL;
	*g_cpp_global_ctors_end = g;
	g_cpp_global_ctors_end = &g->next;
}

/* Emit `void __mxx_global_var_init(void)` that runs every recorded
 * global constructor, then place its address in .init_array. */
static void
cpp_emit_global_ctors(void)
{
	extern struct scope filescope;
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);
	extern struct func *mkfunc(struct decl *, char *, struct type *,
	    struct scope *);
	extern void delfunc(struct func *);
	extern void emitfunc(struct func *, struct scope *, bool);
	extern void funcret(struct func *, struct value *);
	extern struct scope *delscope(struct scope *);
	extern void tokpush(struct token *, size_t);

	struct cpp_global_ctor *g;
	struct decl *d;
	struct scope *fs;
	struct func *f;
	struct type *vt;

	if (!g_cpp_global_ctors)
		return;

	vt = mktype(TYPEFUNC, 0);
	vt->base = &typevoid;
	vt->u.func.params = NULL;
	vt->u.func.nparam = 0;
	vt->u.func.isvararg = false;   /* mktype leaves u.func uninitialized */

	d = mkdecl("__mxx_global_var_init", DECLFUNC, vt, QUALNONE, LINKEXTERN);
	d->value = mkglobal(d);
	fs = mkscope(&filescope);
	f = mkfunc(d, d->name, vt, fs);
	for (g = g_cpp_global_ctors; g; g = g->next)
		cpp_emit_ctor_call(f, g->d, g->args);
	funcret(f, NULL);
	emitfunc(f, fs, true);
	delfunc(f);
	delscope(fs);

	/* register in .init_array so the runtime calls it before main */
	printf(".section .init_array,\"aw\"\n");
	printf(".balign 8\n");
	printf(".quad __mxx_global_var_init\n");

	/* Reverse-order destruction: `void __mxx_global_var_fini(void)`
	 * calls each global's destructor in reverse construction order and
	 * is registered in .fini_array so the runtime runs it after main. */
	vt = mktype(TYPEFUNC, 0);
	vt->base = &typevoid;
	vt->u.func.params = NULL;
	vt->u.func.nparam = 0;
	vt->u.func.isvararg = false;

	d = mkdecl("__mxx_global_var_fini", DECLFUNC, vt, QUALNONE, LINKEXTERN);
	d->value = mkglobal(d);
	fs = mkscope(&filescope);
	f = mkfunc(d, d->name, vt, fs);
	{
		/* reverse traversal: two pointers walk the list */
		struct cpp_global_ctor *tail = NULL, *cur = g_cpp_global_ctors;
		while (cur) {
			struct cpp_global_ctor *next = cur->next;
			cur->next = tail;
			tail = cur;
			cur = next;
		}
		for (g = tail; g; g = g->next)
			cpp_emit_global_dtor(f, g->d);
	}
	funcret(f, NULL);
	emitfunc(f, fs, true);
	delfunc(f);
	delscope(fs);

	printf(".section .fini_array,\"aw\"\n");
	printf(".balign 8\n");
	printf(".quad __mxx_global_var_fini\n");
}

/* Parse one method body (replayed token stream is positioned at '{'). */
static void
cpp_parse_method_body(struct cpp_pending_method *pm)
{
	extern struct func *mkfunc(struct decl *, char *, struct type *,
	    struct scope *);
	extern void delfunc(struct func *);
	extern void stmt(struct func *, struct scope *);
	extern void emitfunc(struct func *, struct scope *, bool);
	extern void funchlt(struct func *);
	extern struct scope *delscope(struct scope *);

	struct scope *fs;
	struct decl *nd;
	struct func *f;

	fs = mkscope(pm->s);
	for (nd = pm->mtype->u.func.params; nd; nd = nd->next)
		if (nd->name) /* unnamed parameters (`B(int)`) have no name to bind */
			scopeputdecl(fs, nd);

	/* method-body context is saved/restored so a nested method-body parse
	 * (an inner class or a lambda closure defined inside this body) does
	 * not clobber the outer context mid-parse */
	{
		struct cpp_method_ctx saved = g_cpp_method;
		g_cpp_method.class_type = pm->classt;
		g_cpp_method.this_decl = pm->is_static ? NULL : pm->thisd;
		g_cpp_method.active = true;

		f = mkfunc(pm->d, pm->d->name, pm->d->type, fs);
		/* constructor: parse the init list (`: Base(v), m(v)`) if any,
		 * then run base-class constructors before the body */
		if (strcmp(pm->mname, pm->tag) == 0) {
			g_cpp_init_items = NULL;
			g_cpp_init_end = &g_cpp_init_items;
			cpp_parse_init_list(f, fs);
			cpp_emit_base_ctor(f);
			/* point every vptr at this class's vtable (the base ctor just set
			 * them to the base view; the complete object needs the final one) */
			cpp_init_vptrs(f, pm->classt, cpp_this_expr());
		}
		stmt(f, fs);
		/* destructor: run base-class destructors after the body (the
		 * derived body runs first, then each base's body via its own
		 * `Base_dtor`, recursively — reverse construction order) */
		if (strcmp(pm->mname, "dtor") == 0)
			cpp_emit_base_dtor(f);
		if (pm->d->u.func.isnoreturn)
			funchlt(f);
		/* C++14 `auto` return type: backfill the type deduced from the
		 * body's return statement(s). */
		if (pm->d->type->base == &typeauto) {
			if (!g_cpp_auto_ret_type)
				error_code(E_DECL, &tok.loc, "'auto' member function '%s' has no return statement to deduce its type from", pm->mname);
			pm->d->type->base = g_cpp_auto_ret_type;
			g_cpp_auto_ret_type = NULL;
			g_cpp_auto_ret_func = NULL;
		}
		emitfunc(f, fs, pm->d->linkage == LINKEXTERN || pm->d->linkage == LINKC);
		delscope(fs);
		delfunc(f);
		pm->d->defined = true;

		g_cpp_method = saved;
	}
}

/* Collect the `{...}` body tokens and queue the method for replay once
 * the enclosing class body is complete. */
static void
buffer_method_body(struct scope *s, struct type *classt, struct type *mtype,
                   struct decl *thisd, struct decl *d,
                   const char *mname, const char *tag, bool is_static)
{
	struct cpp_pending_method *pm;
	size_t cap = 0;
	int bd = 0;

	pm = xmalloc(sizeof(*pm));
	pm->toks = NULL;
	pm->ntoks = 0;
	pm->mname = mname;
	pm->tag = tag;
	pm->classt = classt;
	pm->mtype = mtype;
	pm->thisd = thisd;
	pm->d = d;
	pm->s = s;
	pm->is_static = is_static;
	pm->nbinds = 0;
	pm->next = NULL;
	if (g_cpp_tmpl_instantiating) {
		pm->nbinds = g_cpp_tmpl_nbinds;
		memcpy(pm->binds, g_cpp_tmpl_binds,
		    (size_t)g_cpp_tmpl_nbinds * sizeof *pm->binds);
	}

	/* a ctor init list (`: Base(v) { ... }`) has no opening brace right
	 * after the declarator: buffer through the body's closing '}'.
	 * `seen` tracks whether we have entered the body yet: the tokens
	 * between ':' and the first '{' (the init items and their argument
	 * expressions) must be buffered too, but brace depth only starts
	 * counting at the body's '{'. */
	if (tok.kind == TCOLON) {
		bool seen = false;
		do {
			if (pm->ntoks >= cap) {
				cap = cap ? cap * 2 : 64;
				pm->toks = xreallocarray(pm->toks, cap, sizeof *pm->toks);
			}
			pm->toks[pm->ntoks++] = tok;
			if (tok.kind == TLBRACE) {
				++bd;
				seen = true;
			} else if (tok.kind == TRBRACE) {
				--bd;
			}
			next();
		} while ((!seen || bd > 0) && tok.kind != TEOF);
		*g_cpp_pending_methods_end = pm;
		g_cpp_pending_methods_end = &pm->next;
		return;
	}

	do {
		if (pm->ntoks >= cap) {
			cap = cap ? cap * 2 : 64;
			pm->toks = xreallocarray(pm->toks, cap, sizeof *pm->toks);
		}
		pm->toks[pm->ntoks++] = tok;
		if (tok.kind == TLBRACE)
			++bd;
		else if (tok.kind == TRBRACE)
			--bd;
		next();
	} while (bd > 0 && tok.kind != TEOF);

	*g_cpp_pending_methods_end = pm;
	g_cpp_pending_methods_end = &pm->next;
}

/* Replay all buffered method bodies (called after the class layout is
 * fixed). */
static void
flush_pending_methods(void)
{
	extern void tokpush(struct token *, size_t);
	struct cpp_pending_method *head, *pm;

	/* D2: during a class-template instantiation, defer every method body
	 * instead of parsing it now.  The deferred bodies are parsed lazily
	 * (cpp_ensure_method_defined) when the member is actually called, so
	 * an unused member with an ill-formed body does not fail the
	 * instantiation. */
	if (g_cpp_tmpl_instantiating) {
		if (g_cpp_pending_methods) {
			*g_cpp_deferred_end = g_cpp_pending_methods;
			while (*g_cpp_deferred_end)
				g_cpp_deferred_end = &(*g_cpp_deferred_end)->next;
			g_cpp_pending_methods = NULL;
			g_cpp_pending_methods_end = &g_cpp_pending_methods;
		}
		return;
	}

	/* Detach the pending list first: a method body that defines an inner
	 * class (or a lambda closure) queues more methods, and an inner
	 * class's flush must not re-process the methods already being handled
	 * here. */
	head = g_cpp_pending_methods;
	g_cpp_pending_methods = NULL;
	g_cpp_pending_methods_end = &g_cpp_pending_methods;

	for (pm = head; pm; pm = pm->next) {
		/* the class-body-following token (e.g. ';') currently sits in the
		 * global tok; push it back so the replayed body is parsed in front
		 * of it and next() returns to it afterwards. */
		struct token cur = tok;
		tokpush(&cur, 1);
		tokpush(pm->toks, pm->ntoks);
		next(); /* position tok at the first replayed token ('{') */
		cpp_parse_method_body(pm);
	}
}

/* Parse a deferred method body (D2) now that the member is actually
 * used.  `fd` is the mangled member decl whose body was deferred during
 * a class-template instantiation; the matching buffered body is removed
 * from the deferred table and parsed.  Returns true when the member is
 * defined afterwards (already-defined members are a no-op success). */
bool
cpp_ensure_method_defined(struct decl *fd)
{
	struct cpp_pending_method *pm, **prev;

	if (!fd || fd->defined)
		return true;
	for (prev = &g_cpp_deferred_methods; (pm = *prev); prev = &pm->next)
		if (pm->d == fd) {
			*prev = pm->next;
			/* removing the tail node would leave g_cpp_deferred_end
			 * dangling (later flushes would append to a detached node
			 * and lose the methods); re-anchor it to the new tail */
			if (!pm->next)
				g_cpp_deferred_end = prev;
			{
				/* Unlike flush_pending_methods (parse-phase), this may
				 * run mid-emit (funcexpr EXPRCALL), so the replayed body
				 * must not disturb the caller's token stream or the
				 * current function being emitted. */
				extern void tokpush(struct token *, size_t);
				extern struct func *curfunc;
				extern struct scope filescope;
				struct func *saved_cf = curfunc;
				struct token cur = tok;
				size_t depth = tokctx_depth();
				int bi;
				/* Re-install this instantiation's template parameter
				 * bindings: a later instantiation of the same template
				 * (`C<int,5>` then `C<int,7>`) has since overwritten the
				 * file-scope names, and this body must see its own. */
				for (bi = 0; bi < pm->nbinds; ++bi)
					scopeputdecl(&filescope, pm->binds[bi]);
				tokpush(&cur, 1);
				tokpush(pm->toks, pm->ntoks);
				next(); /* position tok at the first replayed token ('{') */
				cpp_parse_method_body(pm);
				tokctx_rewind(depth);
				tok = cur;
				curfunc = saved_cf;
			}
			free(pm->toks);
			free(pm);
			return true;
		}
	return fd->defined;
}

/* Operator-overload mangling code for a punctuation token: `operator+`
 * lowers to the method name `operator_pl`, mangled `Class_operator_pl`.
 * Returns NULL for operators without a user-overloadable spelling. */
const char *
cpp_op_mangle(enum tokenkind op)
{
	switch (op) {
	case TADD:     return "pl";
	case TSUB:     return "mi";
	case TMUL:     return "ml";
	case TDIV:     return "dv";
	case TMOD:     return "rm";
	case TEQL:     return "eq";
	case TNEQ:     return "ne";
	case TLESS:    return "lt";
	case TLEQ:     return "le";
	case TGREATER: return "gt";
	case TGEQ:     return "ge";
	case TINC:     return "pp";
	case TDEC:     return "mm";
	case TBAND:    return "ad";
	case TBOR:     return "or";
	case TXOR:     return "er";
	case TLNOT:    return "nt";
	case TLPAREN:  return "cl";   /* operator() — functors / lambdas */
	case TLBRACK:  return "ix";   /* operator[] — subscript (C++23 P2128) */
	case TSPACESHIP: return "ss"; /* operator<=> — three-way comparison */
	default:       return NULL;
	}
}

/* Define a static data member out-of-line: `int Class::count = 0;`.  The
 * declarator already mangled the name to `Class_count`; find the in-class
 * declaration and emit its storage. */
void
cpp_define_static_data(struct scope *s, const char *qclass, const char *name)
{
	extern struct scope filescope;
	extern struct init *parseinit(struct scope *, struct type *);
	extern void emitdata(struct decl *, struct init *);

	struct type *ct;
	struct decl *d;
	struct init *init = NULL;

	ct = scopegettag(s, qclass, true);
	if (!ct || (ct->kind != TYPESTRUCT && ct->kind != TYPEUNION))
		error_code(E_CTYPE, &tok.loc, "'%s' is not a class type", qclass);
	d = scopegetdecl(ct->scope ? ct->scope : &filescope, name, 1);
	if (!d || d->kind != DECLOBJECT) {
		error_code(E_DECL, &tok.loc, "no static data member '%s' in class '%s'",
		      name, qclass);
		return;
	}
	if (tok.kind == TASSIGN) {
		next();
		init = parseinit(s, d->type);
	} else if (!d->defined && !d->tentative) {
		/* `int Class::count;` without initializer is a tentative
		 * definition; defer to the normal sweep. */
		d->tentative = true;
	}
	if (tok.kind == TSEMICOLON)
		next();
	if (init || !d->tentative) {
		emitdata(d, init);
		d->defined = true;
	}
}

/* Build a member operator call `l.operator_<mname>(r)` when the class
 * `t` has that operator.  const-K × reference-R 级联查找：声明侧形如
 * `Vec_operator_eqKRoVec`（const 成员函数追加 K，引用形参前缀 R）。调用
 * 侧按实参编码，缺 K/R 会查不到。依次尝试 4 种变体：对象 const 匹配的 K
 * 态优先，引用编码（prefer_ref 给 lvalue 加 'R'、rvalue 加 'V'）与裸编码
 * 各试一轮，命中即用。  Returns true and sets *out on success. */
static bool
cpp_member_op_call(struct type *t, const char *mname, struct expr *l,
                   struct expr *r, struct expr **out)
{
	extern struct scope filescope;

	char mangled[256];
	struct decl *fd;
	struct expr *fn, *obj, *call, **end;
	bool obj_const = (l->qual & QUALCONST) != 0;
	bool found = false;
	int kk;

	for (kk = 0; kk < 2 && !found; kk++) {
		/* kk=0 先试与对象 const 性匹配的 K 态，kk=1 回退另一态 */
		const char *ks = (kk == 0) == obj_const ? "K" : "";
		char mnameQ[64];
		int rref;
		snprintf(mnameQ, sizeof mnameQ, "%s%s", mname, ks);
		for (rref = 0; rref < 2 && !found; rref++) {
			cpp_mangled_name_args(t, mnameQ, r, mangled,
			    sizeof mangled, rref != 0);
			fd = scopegetdecl(t->scope ? t->scope : &filescope,
			    mangled, 1);
			found = fd && fd->kind == DECLFUNC;
		}
	}
	if (!found)
		return false;

	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn); /* &Class_operator_pl */

	obj = mkunaryexpr(TBAND, l); /* &l */
	obj->type = mkpointertype(t, l->qual);

	call = mkexpr(EXPRCALL, fd->type->base, fn);
	call->u.call.args = obj;
	call->u.call.nargs = 1;
	end = &obj->next;
	if (r) {
		struct decl *pp = fd->type->u.func.params ?
		    fd->type->u.func.params->next : NULL;
		struct expr *arg = r;
		/* C++ reference parameter: bind the address
		 * (expr_postfix.c:352-354 惯例) */
		if (pp && pp->type && pp->type->isref)
			arg = mkunaryexpr(TBAND, r);
		*end = exprassign(arg, pp ? pp->type : NULL);
		end = &(*end)->next;
		++call->u.call.nargs;
	}
	*out = call;
	return true;
}

/* Lower `l op r` to a member operator call `l.operator_pl(r)` when the
 * left operand is a class type with that operator overloaded.  Falls
 * back to the C++20 rewritten candidates ([over.match.oper]/3.4): with
 * no direct `operator<`/`==`/..., `a op b` rewrites to `(a <=> b) op 0`
 * using the class's `operator<=>`.  Returns true and sets *out on
 * success (caller keeps normal arithmetic). */
bool
cpp_try_operator_call(struct scope *s, struct expr *l, enum tokenkind op,
                      struct expr *r, struct expr **out)
{
	extern struct scope filescope;

	struct type *t = l ? l->type : NULL;
	const char *opcode;
	char mname[64];
	struct decl *fd;
	struct expr *fn, *call, **end;

	opcode = cpp_op_mangle(op);
	if (!opcode || !t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return false;
	snprintf(mname, sizeof mname, "operator_%s", opcode);
	if (cpp_is_member_function(t, mname))
		return cpp_member_op_call(t, mname, l, r, out);
	/* C++20 rewritten candidates: with no direct member operator,
	 * `a < b` / `a == b` / ... rewrite to `(a <=> b) < 0` / `(a <=> b)
	 * == 0` / ... via the class's `operator<=>`. */
	if ((op == TLESS || op == TLEQ || op == TGREATER || op == TGEQ ||
	    op == TEQL || op == TNEQ) &&
	    cpp_is_member_function(t, "operator_ss")) {
		struct expr *ss;
		if (cpp_member_op_call(t, "operator_ss", l, r, &ss) &&
		    ss->type && (ss->type->prop & PROPREAL)) {
			*out = mkbinaryexpr(&tok.loc, op, ss,
			    mkconstexpr(&typeint, 0));
			return true;
		}
	}
	/* non-member operator overload: `operator_pl(a, b)` registered as a
	 * free function in the current scope */
	fd = scopegetdecl(s, mname, 1);
	if (!fd || fd->kind != DECLFUNC)
		return false;
	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn); /* &operator_pl */

	call = mkexpr(EXPRCALL, fd->type->base, fn);
	call->u.call.args = NULL;
	call->u.call.nargs = 0;
	end = &call->u.call.args;
	{
		struct decl *pp = fd->type->u.func.params;
		struct expr *arg = l;
		if (pp && pp->type && pp->type->isref)
			arg = mkunaryexpr(TBAND, l);
		*end = exprassign(arg, pp ? pp->type : NULL);
		end = &(*end)->next;
		++call->u.call.nargs;
		pp = pp ? pp->next : NULL;
		if (r) {
			arg = r;
			if (pp && pp->type && pp->type->isref)
				arg = mkunaryexpr(TBAND, r);
			*end = exprassign(arg, pp ? pp->type : NULL);
			end = &(*end)->next;
			++call->u.call.nargs;
		}
	}
	*out = call;
	return true;
}

/* Lower `obj[args...]` to a member operator[] call
 * `obj.operator_ix(args...)`.  The subscript is a postfix operator, so
 * unlike cpp_try_operator_call (binary `l op r`) the object is always the
 * first argument and the bracket contents are a comma-separated argument
 * list — C++23 P2128 allows operator[] to take any number of parameters.
 * Returns true and sets *out on success (caller keeps the builtin
 * subscript error). */
bool
cpp_subscript_call(struct scope *s, struct expr *obj, struct expr *args,
                   struct expr **out)
{
	extern struct scope filescope;

	struct type *t = obj ? obj->type : NULL;
	const char *mname = "operator_ix";
	char mangled[512];
	struct decl *fd;
	struct expr *fn, *o, *call, **end;
	struct decl *param;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return false;
	/* member operator[] first.  A const member mangles with a K right
	 * after the method name (`Vec_operator_ixKi`).  Resolve the overload
	 * from the object's constness like the ordinary member-call path
	 * (cpp_parse.c this_decl const): a const object must pick the const
	 * (K) overload, a non-const object prefers the non-const one and
	 * falls back to a const method. */
	if (cpp_is_member_function(t, mname)) {
		bool obj_const = (obj->qual & QUALCONST) != 0;
		char mnameQ[64];

		snprintf(mnameQ, sizeof mnameQ, "%s%s", mname,
		    obj_const ? "K" : "");
		cpp_mangled_name_args(t, mnameQ, args, mangled, sizeof mangled, false);
		fd = scopegetdecl(t->scope ? t->scope : &filescope, mangled, 1);
		if (!fd || fd->kind != DECLFUNC) {
			snprintf(mnameQ, sizeof mnameQ, "%s%s", mname,
			    obj_const ? "" : "K");
			cpp_mangled_name_args(t, mnameQ, args, mangled, sizeof mangled, false);
			fd = scopegetdecl(t->scope ? t->scope : &filescope, mangled, 1);
		}
		if (fd && fd->kind == DECLFUNC) {
			fn = mkexpr(EXPRIDENT, fd->type, NULL);
			fn->u.ident.decl = fd;
			fn = decay(fn); /* &Class_operator_ix */

			o = mkunaryexpr(TBAND, obj); /* &obj */
			o->type = mkpointertype(t, obj->qual);

			call = mkexpr(EXPRCALL, fd->type->base, fn);
			call->u.call.args = o;
			call->u.call.nargs = 1;
			end = &o->next;
			/* mtype param[0] is the implicit `this`; explicit params
			 * follow. */
			param = fd->type->u.func.params ? fd->type->u.func.params->next
			                                : NULL;
			for (; args; args = args->next, param = param ? param->next : NULL) {
				*end = exprassign(args, param ? param->type : NULL);
				end = &(*end)->next;
				++call->u.call.nargs;
			}
			*out = call;
			return true;
		}
	}
	/* C++23 P1169 static operator[]: `static int& operator[](Matrix& m,
	 * int i, int j)`.  The object is an explicit first parameter and the
	 * member has no implicit `this`, so the mangled name is
	 * `Class_operator_ix<objparam><args>S` — the object-parameter type
	 * (reference 'R'/'V' or by-value before the class code) is encoded
	 * first, then the bracket args, then the static-member "S" suffix.
	 * The encodings are tried in value-category order (an lvalue object
	 * most likely binds a `T&` object parameter). */
	{
		const char *ord[3];
		int oi;
		struct type *owner = NULL;
		const char *tag;

		if (cpp_method_member(t, mname, &owner) && owner)
			tag = owner->u.structunion.tag;
		else
			tag = t->u.structunion.tag;
		if (!tag)
			tag = "anon";
		ord[0] = (obj && obj->lvalue) ? "R" : "V";
		ord[1] = "";
		ord[2] = (obj && obj->lvalue) ? "V" : "R";
		for (oi = 0; oi < 3; oi++) {
			char argcodes[256];
			char *p = argcodes;
			char *e = argcodes + sizeof argcodes - 1;
			struct expr *a;

			/* the bracket args encode once with plain param types (a
			 * static declaration has no R/V lvalue prefixes) */
			for (a = args; a && p < e; a = a->next) {
				char code[64];
				size_t cl;
				cpp_mangle_type(a->type, code, sizeof code);
				cl = strlen(code);
				if (p + cl >= e)
					break;
				memcpy(p, code, cl);
				p += cl;
			}
			*p = '\0';
			snprintf(mangled, sizeof mangled, "%s_%s%so%s%sS",
			    tag, mname, ord[oi], tag, argcodes);
			fd = scopegetdecl(t->scope ? t->scope : &filescope,
			    mangled, 1);
			if (!fd || fd->kind != DECLFUNC)
				continue;
			fn = mkexpr(EXPRIDENT, fd->type, NULL);
			fn->u.ident.decl = fd;
			fn = decay(fn); /* &Class_operator_ix...S */

			call = mkexpr(EXPRCALL, fd->type->base, fn);
			call->u.call.args = NULL;
			call->u.call.nargs = 0;
			end = &call->u.call.args;
			/* fd's first parameter is the explicit object parameter
			 * (no implicit `this`); bind the object by address when it
			 * is a reference, like every other reference param */
			param = fd->type->u.func.params;
			o = obj;
			if (param && param->type && param->type->isref)
				o = mkunaryexpr(TBAND, obj);
			*end = exprassign(o, param ? param->type : NULL);
			end = &(*end)->next;
			++call->u.call.nargs;
			param = param ? param->next : NULL;
			for (; args; args = args->next,
			    param = param ? param->next : NULL) {
				struct expr *arg = args;
				if (param && param->type && param->type->isref)
					arg = mkunaryexpr(TBAND, args);
				*end = exprassign(arg, param ? param->type : NULL);
				end = &(*end)->next;
				++call->u.call.nargs;
			}
			*out = call;
			return true;
		}
	}
	/* non-member operator[]: `operator_ix(obj, args...)` registered as a
	 * free function (C++23 P2128R8 allows non-member subscripts) */
	fd = scopegetdecl(s, mname, 1);
	if (!fd || fd->kind != DECLFUNC)
		return false;
	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn);
	call = mkexpr(EXPRCALL, fd->type->base, fn);
	call->u.call.args = NULL;
	call->u.call.nargs = 0;
	end = &call->u.call.args;
	param = fd->type->u.func.params;
	*end = exprassign(obj, param ? param->type : NULL);
	end = &(*end)->next;
	++call->u.call.nargs;
	param = param ? param->next : NULL;
	for (; args; args = args->next, param = param ? param->next : NULL) {
		*end = exprassign(args, param ? param->type : NULL);
		end = &(*end)->next;
		++call->u.call.nargs;
	}
	*out = call;
	return true;
}

/* Construct a temporary class object: `Vec(expr)` lowers to allocating an
 * anonymous temporary, running the constructor, and yielding the
 * temporary as an lvalue expression.  Returns the expression (the
 * temporary's value), or NULL if the class has no matching constructor. */
struct expr *
cpp_temp_construct(struct scope *s, struct type *ct)
{
	extern struct func *curfunc;
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);
	extern void funcinit(struct func *, struct decl *, struct init *,
	    bool);

	struct expr *args = NULL, **ae = &args;
	struct expr *e;
	struct decl *tmp;

	if (!ct || (ct->kind != TYPESTRUCT && ct->kind != TYPEUNION))
		return NULL;

	next(); /* consume '(' */
	while (tok.kind != TRPAREN) {
		if (args)
			expect(TCOMMA, "or ')' after constructor argument");
		*ae = assignexpr(s);
		ae = &(*ae)->next;
	}
	next(); /* consume ')' */

	if (!curfunc)
		return NULL;

	/* anonymous temporary */
	tmp = mkdecl("tmp", DECLOBJECT, ct, QUALNONE, LINKNONE);
	tmp->u.obj.storage = SDAUTO;
	funcinit(curfunc, tmp, NULL, false); /* allocate storage */
	cpp_emit_ctor_call(curfunc, tmp, args);

	e = mkexpr(EXPRIDENT, ct, NULL);
	e->qual = QUALNONE;
	/* anonymous temporary is an rvalue: the value-category marker drives
	 * overload resolution so a temporary prefers the move/rvalue overload
	 * over the copy/lvalue one.  IR generation addresses it via its decl
	 * regardless of this flag. */
	e->lvalue = false;
	e->u.ident.decl = tmp;
	return e;
}

/* Is `t` the class whose method body is currently being parsed?  Inside a
 * method body, bare member names resolve (cpp_member_ident) and direct
 * member access is allowed regardless of access level. */
bool
cpp_same_class_context(struct type *t)
{
	if (!g_cpp_method.active || !g_cpp_method.class_type)
		return false;
	return g_cpp_method.class_type == t;
}

/* Enforce C++ access control on `obj.member` / `obj->member` access:
 * private members are only reachable from within the member's own class;
 * protected members additionally from derived classes (friend and
 * virtual inheritance are later stages).  Returns true when the access
 * is allowed. */
bool
cpp_member_accessible(struct type *t, struct member *m)
{
	struct cpp_friend *fr;

	if (!m || m->access == ACC_PUBLIC)
		return true;
	if (m->access == ACC_PROTECTED && cpp_is_derived(g_cpp_method.class_type, t))
		return true;
	if (cpp_same_class_context(t))
		return true;
	/* friend classes of `t` (recorded by `friend class B;` in its body)
	 * may access its private/protected members from their own methods;
	 * friend free functions are covered by cpp_friend_decl pointing the
	 * method context at the befriending class while their body is parsed */
	if (g_cpp_method.active && g_cpp_method.class_type)
		for (fr = t->u.structunion.friends; fr; fr = fr->next)
			if (fr->cls == g_cpp_method.class_type)
				return true;
	return false;
}

/* Define a member function as an out-of-line free function named
 * `ClassName_method` (class_tag is the enclosing struct/class tag).
 * Reuses the C function-definition machinery (mkdecl/mkfunc/stmt) via a
 * small clone of decl()'s DECLFUNC path.  The implicit `this` parameter
 * (Class *) is prepended to the mangled signature; inside the body, bare
 * member names resolve to `(*this).name` via cpp_member_ident. */
void
cpp_define_method(struct scope *s, struct type *funct, const char *mname,
                  const char *class_tag, bool is_const, bool is_static,
                  bool is_virtual)
{
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);

	char mangled[256];
	char *pmangled;
	struct type *mtype, *classt;
	struct decl *d, *thisd, *cur, *nd, **end;

	if (!class_tag || !mname)
		return;

	{
		struct scope *qns = cpp_take_qual_ns();
		classt = scopegettag(qns ? qns : s, class_tag, true);
	}
	if (!classt || (classt->kind != TYPESTRUCT && classt->kind != TYPEUNION))
		error_code(E_CTYPE, &tok.loc, "'%s' is not a class type", class_tag);

	/* C++ virtual member (C.2.5): register the slot identity in the
	 * class's own_virtuals list (deduped across in-class decl + out-of-line
	 * definition); cpp_compute_vtable lays out the final slot indices.
	 * An override keeps the virtual-ness of the base method even without
	 * an explicit `virtual` keyword, so look the signature up in the
	 * bases' vtable layouts too. */
	{
		bool eff_virtual = is_virtual;
		char key[256];
		struct member *bm;

		g_cpp_define_virtual = false;
		if (!is_static) {
			cpp_vkey(mname, funct, is_const, key, sizeof key);
			if (!eff_virtual) {
				for (bm = classt->u.structunion.members; bm; bm = bm->next)
					if (!bm->name && bm->type &&
					    (bm->type->kind == TYPESTRUCT ||
					     bm->type->kind == TYPEUNION) &&
					    cpp_find_final(bm->type, key, NULL, NULL)) {
						eff_virtual = true;
						break;
					}
			}
			if (eff_virtual) {
				struct cpp_vslot *vs, **ve;
				for (vs = classt->u.structunion.own_virtuals; vs; vs = vs->next)
					if (strcmp(vs->key, key) == 0)
						break;
				if (!vs) {
					vs = xmalloc(sizeof *vs);
					vs->name = xmalloc(strlen(mname) + 1);
					strcpy((char *)vs->name, mname);
					memcpy(vs->key, key, sizeof key);
					vs->m = NULL;
					vs->owner = classt;
					vs->index = -1;
					vs->next = NULL;
					ve = &classt->u.structunion.own_virtuals;
					while (*ve)
						ve = &(*ve)->next;
					*ve = vs;
				}
				classt->u.structunion.own_poly = true;
				g_cpp_define_virtual = true;
			}
		}
	}

	/* deducing-this (P0847): a leading `this X& self` explicit object
	 * parameter replaces the implicit `this`.  It is excluded from the
	 * mangled overload signature (the object is passed like `this`), its
	 * cv-qualification selects the const "K" form, and an `X&&` object
	 * param gets a trailing "V" so the rvalue overload mangles apart from
	 * the lvalue `X&` form (the call site tries the V variant for
	 * temporary objects). */
	{
		struct decl *exobj = cpp_explicit_obj_take();
		bool has_exobj = exobj && funct->u.func.params == exobj;
		bool eff_const = is_const ||
		    (has_exobj && exobj->type->kind == TYPEPOINTER &&
		     (exobj->type->qual & QUALCONST));
		bool exobj_rref = has_exobj && exobj->type->isrref;
		struct decl *params = has_exobj ? funct->u.func.params->next
		                                : funct->u.func.params;

		snprintf(mangled, sizeof mangled, "%s_%s", class_tag, mname);
		/* const member functions get a distinct mangled name so a const
		 * object can only call const methods */
		if (eff_const)
			strncat(mangled, "K", sizeof mangled - strlen(mangled) - 1);
		/* overload resolution: append the encoded explicit parameter types
		 * (`Class_method_ii`); no-arg methods keep the bare mangled name */
		for (cur = params; cur; cur = cur->next) {
			char code[64];
			cpp_mangle_type(cur->type, code, sizeof code);
			strncat(mangled, code, sizeof mangled - strlen(mangled) - 1);
		}
		/* static members get a distinct mangled name (no `this`); the S
		 * goes after the parameter encoding to match
		 * cpp_mangled_name_args + "S" */
		if (is_static)
			strncat(mangled, "S", sizeof mangled - strlen(mangled) - 1);
		/* rvalue-object (`this X&& self`) overload: trailing V */
		if (exobj_rref)
			strncat(mangled, "V", sizeof mangled - strlen(mangled) - 1);
		/* mkdecl/scopeputdecl keep the name pointer; persist it off the
		 * stack (the C parser's token strings are stable, ours is not). */
		pmangled = xmalloc(strlen(mangled) + 1);
		strcpy(pmangled, mangled);

		/* Build the mangled function type:
		 * `Class_method(Class *this, args...) -> funct->base` (or just
		 * `Class_method(args...)` for a static member).  The declarator
		 * already parsed the explicit params into funct; we copy those
		 * decls so funct (kept in the member list for call lowering) and
		 * mtype don't share the same decl chain. */
		mtype = mktype(TYPEFUNC, 0);
		mtype->base = funct->base;
		mtype->qual = funct->qual;
		mtype->prop |= funct->prop;
		mtype->align = funct->align;
		mtype->u.func.isvararg = funct->u.func.isvararg;
		mtype->u.func.params = NULL;
		mtype->u.func.nparam = 0;
		thisd = NULL;
		end = &mtype->u.func.params;
		if (!is_static) {
			if (has_exobj) {
				/* deducing-this: the explicit object parameter is mtype
				 * param[0], so the method-body scope binds `self` (a
				 * reference that auto-derefs on use); the call site
				 * passes &obj, which is its lowered pointer.  The body's
				 * `this` resolves to the object parameter too
				 * (cpp_this_expr emits &(*self)). */
				struct decl *objd = mkdecl(exobj->name, DECLOBJECT,
				    exobj->type, exobj->qual, LINKNONE);
				objd->u.obj.storage = SDAUTO;
				*end = objd;
				end = &objd->next;
				++mtype->u.func.nparam;
				thisd = objd;
			} else {
				thisd = mkdecl("this", DECLOBJECT,
				    mkpointertype(classt,
				        eff_const ? QUALCONST : QUALNONE),
				    QUALNONE, LINKNONE);
				thisd->u.obj.storage = SDAUTO;
				*end = thisd;
				end = &thisd->next;
				++mtype->u.func.nparam;
			}
		}
		for (cur = params; cur; cur = cur->next) {
			nd = mkdecl(cur->name, DECLOBJECT, cur->type, cur->qual,
			            LINKNONE);
			nd->u.obj.storage = SDAUTO;
			*end = nd;
			end = &nd->next;
			++mtype->u.func.nparam;
		}
	}

	/* Register the mangled function symbol in the class's scope (the
	 * namespace scope for `namespace n { class C { ... }; }`) so the
	 * call lowering (postfixexpr TPERIOD) can resolve it from the
	 * object's class type. */
	{
		struct scope *ms = classt->scope ? classt->scope : s;
		d = scopegetdecl(ms, mangled, false);
		if (d && d->kind != DECLFUNC)
			error_code(E_REDEF, &tok.loc, "'%s' redeclared with different kind", mangled);
		if (d && d->type && !typecompatible(mtype, d->type))
			error_code(E_REDEF, &tok.loc, "'%s' redeclared with incompatible type", mangled);
		if (d && d->defined)
			error_tok_code(E_REDEF, &tok, "redefinition of member function '%s'", mangled);
		if (!d) {
			d = mkdecl(pmangled, DECLFUNC, mtype, QUALNONE, LINKEXTERN);
			scopeputdecl(ms, d);
		} else {
			d->type = typecomposite(mtype, d->type);
			free(pmangled);
		}
	}
	d->value = mkglobal(d);

	/* a ctor init list (`: Base(v)`) also means a function definition; the
	 * buffered body (from the ':' through the closing '}') is parsed by
	 * cpp_parse_method_body, which consumes the init list first */
	if (tok.kind != TLBRACE && tok.kind != TCOLON) {
		if (tok.kind == TSEMICOLON)
			next();
		return; /* declaration only */
	}

	/* Function definition.  Inside a class body the layout is not fixed
	 * yet, so buffer the body tokens and parse it after the class closes
	 * (two-phase: method bodies may use members declared later). */
	if (g_cpp_class_parsing) {
		buffer_method_body(s, classt, mtype, thisd, d, mname, class_tag,
		                   is_static);
		return;
	}
	{
		struct cpp_pending_method pm;
		pm.toks = NULL;
		pm.ntoks = 0;
		pm.mname = mname;
		pm.tag = class_tag;
		pm.classt = classt;
		pm.mtype = mtype;
		pm.thisd = thisd;
		pm.d = d;
		pm.s = s;
		pm.is_static = is_static;
		cpp_parse_method_body(&pm);
	}
}

/* Append one token to a synthesized token stream (cpp_synth_default_spaceship). */
static void
cpp_ss_addtok(struct token **toks, size_t *n, enum tokenkind k,
    const char *lit, struct location loc)
{
	extern int tokenget(const void *, size_t);

	*toks = xreallocarray(*toks, *n + 1, sizeof **toks);
	/* identifiers get their per-name kind via tokenget (the same path
	 * the scanner uses): all identifiers share the TIDENT enum value,
	 * but the macro table and name lookups key on the per-name kind */
	if (k == TIDENT && lit)
		k = tokenget(lit, strlen(lit));
	(*toks)[*n].kind = k;
	(*toks)[*n].lit = (char *)lit;
	(*toks)[*n].loc = loc;
	(*toks)[*n].hide = false;
	(*toks)[*n].space = false;
	++*n;
}

/* Synthesize the body of a C++20 defaulted `<=>` (P0515):
 * `auto operator<=>(const T&) const = default;` compares every
 * non-static data member in declaration order and returns the first
 * non-zero result, else 0:
 *
 *     { if (x <=> rhs.x != 0) return x <=> rhs.x; ... return 0; }
 *
 * The synthesized body is tokenized and replayed through the normal
 * method-body path (cpp_define_method buffers it while the class body is
 * still being parsed and flushes it after layout), so the implicit `this`
 * and bare-member lowering (cpp_member_ident) apply as usual.  An unnamed
 * parameter (`const T&`) is given an internal name so the body can
 * reference the right-hand operand.  Base-class subobjects (anonymous
 * members) and static data members (not in the member list) are skipped. */
void
cpp_synth_default_spaceship(struct scope *s, struct type *funct,
    const char *mname, const char *class_tag, bool is_const)
{
	extern void tokpush(struct token *, size_t);
	extern void next(void);

	struct type *ct;
	struct member *m;
	struct decl *pd = funct->u.func.params;
	const char *rhs = "__mcc_ss_rhs";
	struct token *toks = NULL;
	size_t n = 0;
	struct location loc = tok.loc;

	/* the synthesized body refers to the RHS operand by parameter name;
	 * an unnamed parameter gets an internal name so the comparison can
	 * reference it */
	if (pd && !pd->name) {
		pd->name = xmalloc(strlen(rhs) + 1);
		strcpy((char *)pd->name, rhs);
	} else if (pd && pd->name) {
		rhs = pd->name;
	}
	if (!pd)
		error_code(E_DECL, &tok.loc,
		    "defaulted 'operator<=>' must take a parameter");

	ct = scopegettag(s, class_tag, true);
	cpp_ss_addtok(&toks, &n, TLBRACE, NULL, loc);
	if (ct) {
		for (m = ct->u.structunion.members; m; m = m->next) {
			/* skip member functions; base-class subobjects are
			 * anonymous members (no name to compare by) */
			if (!m->name || m->type->kind == TYPEFUNC)
				continue;
			/* if (m <=> rhs.m != 0) return m <=> rhs.m; */
			cpp_ss_addtok(&toks, &n, TIF, NULL, loc);
			cpp_ss_addtok(&toks, &n, TLPAREN, NULL, loc);
			cpp_ss_addtok(&toks, &n, TIDENT, m->name, loc);
			cpp_ss_addtok(&toks, &n, TSPACESHIP, NULL, loc);
			cpp_ss_addtok(&toks, &n, TIDENT, rhs, loc);
			cpp_ss_addtok(&toks, &n, TPERIOD, NULL, loc);
			cpp_ss_addtok(&toks, &n, TIDENT, m->name, loc);
			cpp_ss_addtok(&toks, &n, TNEQ, NULL, loc);
			cpp_ss_addtok(&toks, &n, TNUMBER, "0", loc);
			cpp_ss_addtok(&toks, &n, TRPAREN, NULL, loc);
			cpp_ss_addtok(&toks, &n, TRETURN, NULL, loc);
			cpp_ss_addtok(&toks, &n, TIDENT, m->name, loc);
			cpp_ss_addtok(&toks, &n, TSPACESHIP, NULL, loc);
			cpp_ss_addtok(&toks, &n, TIDENT, rhs, loc);
			cpp_ss_addtok(&toks, &n, TPERIOD, NULL, loc);
			cpp_ss_addtok(&toks, &n, TIDENT, m->name, loc);
			cpp_ss_addtok(&toks, &n, TSEMICOLON, NULL, loc);
		}
	}
	cpp_ss_addtok(&toks, &n, TRETURN, NULL, loc);
	cpp_ss_addtok(&toks, &n, TNUMBER, "0", loc);
	cpp_ss_addtok(&toks, &n, TSEMICOLON, NULL, loc);
	cpp_ss_addtok(&toks, &n, TRBRACE, NULL, loc);

	/* replay through the normal method-definition path: keep the token
	 * after the consumed ';' (the next class member or '}') on the
	 * stream, push the synthesized body in front, and let
	 * cpp_define_method buffer it (in-class, g_cpp_class_parsing) or
	 * parse it directly */
	{
		struct token cur = tok;
		tokpush(&cur, 1);
		tokpush(toks, n);
		next(); /* position at the body's '{' */
		cpp_define_method(s, funct, mname, class_tag, is_const, false,
		    false);
	}
	free(toks);
}

/* Non-member operator overload: `Vec operator+(Vec a, Vec b) {...}`
 * defines a free `operator_pl` function (no class/this).  The return
 * type has already been parsed by declspecs (`base`); here we consume
 * the `operator` keyword, the operator token, and the parameter list,
 * then register the symbol and parse the body. */
void
cpp_parse_free_operator(struct scope *s, struct qualtype base)
{
	extern struct func *mkfunc(struct decl *, char *, struct type *,
	    struct scope *);
	extern void delfunc(struct func *);
	extern void stmt(struct func *, struct scope *);
	extern void emitfunc(struct func *, struct scope *, bool);
	extern struct scope *delscope(struct scope *);

	const char *opcode;
	char mname[64], *pmangled;
	struct type *ft;
	struct decl *pd, *d, **pend;
	struct decl *nd;
	struct scope *fs;
	struct func *f;

	if (cpp_tok_kind() != CPP_TOPERATOR)
		error_code(E_SYNTAX, &tok.loc, "expected 'operator'");
	next(); /* consume 'operator' */
	/* C++11 user-defined literal: `operator""_km` — the `""` string
	 * literal token is followed by the user suffix identifier (`_km`).
	 * Lowered to the free-function name `operator_udl_km` (standard
	 * non-`_` suffixes are reserved and never reach here). */
	if (tok.kind == TSTRINGLIT) {
		const char *sfx;
		if (strcmp(tok.lit, "\"\"") != 0)
			error_code(E_OVERLOAD, &tok.loc,
			    "user-defined literal must use operator\"\"");
		next(); /* consume "" */
		if (tok.kind < TIDENT)
			error_code(E_SYNTAX, &tok.loc,
			    "expected suffix identifier after operator\"\"");
		sfx = tokenstr(tok.kind);
		if (sfx[0] != '_')
			error_code(E_OVERLOAD, &tok.loc,
			    "user-defined literal suffix must begin with '_'");
		next(); /* consume the suffix identifier */
		snprintf(mname, sizeof mname, "operator_udl_%s", sfx + 1);
	} else {
		opcode = cpp_op_mangle(tok.kind);
		if (!opcode)
			error_code(E_OVERLOAD, &tok.loc, "unsupported operator for overloading");
		next(); /* consume the operator token */
		/* operator()/operator[]: the closing ')' / ']' of the operator
		 * token follows; the next '(' is the parameter list. */
		if (strcmp(opcode, "cl") == 0)
			expect(TRPAREN, "after 'operator()'");
		else if (strcmp(opcode, "ix") == 0)
			expect(TRBRACK, "after 'operator[]'");
		snprintf(mname, sizeof mname, "operator_%s", opcode);
	}

	ft = mktype(TYPEFUNC, 0);
	ft->qual = QUALNONE;
	ft->base = base.type; /* return type */
	ft->u.func.isvararg = false;
	ft->u.func.params = NULL;
	ft->u.func.nparam = 0;
	pend = &ft->u.func.params;
	if (tok.kind == TLPAREN) {
		next();
		while (tok.kind != TRPAREN) {
			pd = parameter(s);
			*pend = pd;
			pend = &pd->next;
			++ft->u.func.nparam;
			if (tok.kind == TRPAREN)
				break;
			expect(TCOMMA, "or ')' after operator parameter");
		}
		next(); /* consume ')' */
	}
	/* mkdecl/scopeputdecl keep the name pointer; persist it off the
	 * stack (token strings from the C parser are stable, ours are not). */
	pmangled = xmalloc(strlen(mname) + 1);
	strcpy(pmangled, mname);

	d = scopegetdecl(s, mname, false);
	if (d && d->kind != DECLFUNC)
		error_code(E_REDEF, &tok.loc, "'%s' redeclared with different kind", mname);
	if (d && d->type && !typecompatible(ft, d->type))
		error_code(E_REDEF, &tok.loc, "'%s' redeclared with incompatible type", mname);
	if (!d) {
		d = mkdecl(pmangled, DECLFUNC, ft, QUALNONE, LINKEXTERN);
		scopeputdecl(s, d);
	} else {
		d->type = typecomposite(ft, d->type);
		free(pmangled);
	}
	d->value = mkglobal(d);

	if (tok.kind != TLBRACE) {
		if (tok.kind == TSEMICOLON)
			next();
		return; /* declaration only */
	}

	/* function definition: mirror the non-class body path */
	fs = mkscope(s);
	for (pd = ft->u.func.params; pd; pd = pd->next)
		if (pd->name) /* unnamed parameters have no name to bind */
			scopeputdecl(fs, pd);
	f = mkfunc(d, d->name, d->type, fs);
	stmt(f, fs);
	emitfunc(f, fs, d->linkage == LINKEXTERN || d->linkage == LINKC);
	delscope(fs);
	delfunc(f);
	d->defined = true;
}

/* C++11 user-defined literal suffix detection.  Given the token text of a
 * number literal (e.g. "1.5_km", "123_km2", "0x1_0000"), return a pointer
 * to the `_suffix` part, or NULL when the text carries no UDL suffix.
 * A UDL suffix is a `_` followed by an identifier start (alphabetic or
 * underscore); a `_` followed by a digit is a C++14 digit separator
 * (`0x1_0000`) and not a suffix.  Scanning back from the end, the first
 * `_` decides: the suffix, if any, is the final `_identifier` run. */
const char *
cpp_udl_suffix_of(const char *lit)
{
	size_t n = strlen(lit);
	size_t i;

	if (n < 2)
		return NULL;
	for (i = n; i > 1; --i) {
		if (lit[i - 1] == '_') {
			unsigned char c = lit[i];
			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
			    c == '_')
				return &lit[i - 1];
			return NULL; /* digit separator / non-suffix '_' */
		}
	}
	return NULL;
}

/* Lower a user-defined literal to a call: `1.5_km` -> `operator_udl_km(e)`.
 * The literal argument `arg` is already parsed (a numeric constant, or the
 * decayed pointer of a string literal).  The UDL function's parameter list
 * drives the binding: a string UDL takes (const char*, size_t), so the
 * literal length (excluding NUL) is appended when a second parameter is
 * declared.  Returns NULL when no matching operator is in scope. */
struct expr *
cpp_udl_literal_call(struct scope *s, const char *sfx, struct expr *arg)
{
	extern struct scope filescope;

	char mname[128];
	struct decl *fd, *pp;
	struct expr *fn, *call, **end;

	snprintf(mname, sizeof mname, "operator_udl_%s", sfx + 1);
	fd = scopegetdecl(s, mname, 1);
	if (!fd || fd->kind != DECLFUNC)
		return NULL;
	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn); /* &operator_udl_km */

	call = mkexpr(EXPRCALL, fd->type->base, fn);
	call->u.call.args = NULL;
	call->u.call.nargs = 0;
	end = &call->u.call.args;
	pp = fd->type->u.func.params;
	{
		struct expr *a = arg;
		if (pp && pp->type && pp->type->isref)
			a = mkunaryexpr(TBAND, arg);
		*end = exprassign(a, pp ? pp->type : NULL);
		end = &(*end)->next;
		++call->u.call.nargs;
	}
	pp = pp ? pp->next : NULL;
	if (pp) {
		/* string UDL: append the literal length (chars, minus NUL) */
		unsigned long long len = 0;
		if (arg->kind == EXPRUNARY && arg->base &&
		    arg->base->kind == EXPRSTRING)
			len = arg->base->u.string.size - 1;
		*end = exprassign(mkconstexpr(&typeulong, len), pp->type);
		++call->u.call.nargs;
	}
	return call;
}

/* C++ member-function lookup helpers.  A function member is registered in
 * the struct/union member list by addmember (C++ mode); these helpers let
 * the postfix-expression lowering detect and mangle member calls.  The
 * lookup recurses through anonymous members, so inherited members (the
 * base-class subobject is an anonymous member at offset 0) resolve to
 * their defining class. */

/* Does `t` (or any of its base subobjects) contain a member named
 * `name`?  Used to count ambiguous inherited members. */
static bool
cpp_base_contains(struct type *t, const char *name)
{
	struct member *m;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return false;
	for (m = t->u.structunion.members; m; m = m->next) {
		if (m->name) {
			if (strcmp(m->name, name) == 0)
				return true;
		} else if (m->type && cpp_base_contains(m->type, name)) {
			return true;
		}
	}
	return false;
}

/* Multiple-inheritance member ambiguity: `obj.member` is ambiguous when
 * the name is not a direct member and is defined by more than one base
 * subobject (C++ [class.member.lookup]).  A direct member hides all
 * inherited ones. */
bool
cpp_member_ambiguous(struct type *t, const char *name)
{
	struct member *m;
	int nbases = 0, found = 0;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return false;
	for (m = t->u.structunion.members; m; m = m->next)
		if (m->name && strcmp(m->name, name) == 0)
			return false;
	for (m = t->u.structunion.members; m; m = m->next) {
		if (!m->name && m->type &&
		    (m->type->kind == TYPESTRUCT || m->type->kind == TYPEUNION)) {
			++nbases;
			if (cpp_base_contains(m->type, name))
				++found;
		}
	}
	return nbases > 1 && found > 1;
}

/* Find the function member `name` in `t`, optionally reporting the class
 * that defines it (`*owner`).  The class's own members are checked before
 * its base-class subobjects (which are anonymous members), so a derived
 * class's method hides a same-named base method — matching C++ name
 * lookup. */
static struct member *
cpp_method_member(struct type *t, const char *name, struct type **owner)
{
	struct member *m, *sub;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return NULL;
	for (m = t->u.structunion.members; m; m = m->next) {
		if (m->name && strcmp(m->name, name) == 0) {
			if (m->type && m->type->kind == TYPEFUNC) {
				if (owner)
					*owner = t;
				return m;
			}
		}
	}
	for (m = t->u.structunion.members; m; m = m->next) {
		if (!m->name) {
			sub = cpp_method_member(m->type, name, owner);
			if (sub)
				return sub;
		}
	}
	return NULL;
}

bool
cpp_is_member_function(struct type *t, const char *name)
{
	return cpp_method_member(t, name, NULL) != NULL;
}

/* Class-qualified member lookup for `obj.Base::get()`: resolve `name`
 * in `t`'s scope, where the qualified class's own members shadow base
 * members, then each direct base in turn (same shadowing rule
 * recursively).  `*offset` accumulates the byte offset from `t`'s start
 * (in caller units).  Unlike typemember (which recurses into a base as
 * soon as it is seen), this makes the qualified class's own member win
 * over an inherited one of the same name. */
struct member *
cpp_qualified_member(struct type *t, const char *name,
    unsigned long long *offset)
{
	struct member *m, *sub;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return NULL;
	for (m = t->u.structunion.members; m; m = m->next) {
		if (m->name && strcmp(m->name, name) == 0) {
			*offset += m->offset;
			return m;
		}
	}
	for (m = t->u.structunion.members; m; m = m->next) {
		if (m->name || !m->type ||
		    (m->type->kind != TYPESTRUCT && m->type->kind != TYPEUNION))
			continue;
		sub = cpp_qualified_member(m->type, name, offset);
		if (sub) {
			*offset += m->offset;
			return sub;
		}
	}
	return NULL;
}

/* Is class `t` derived from (or identical to) class `base`?  The base
 * subobject appears as an anonymous member of the derived class. */
bool
cpp_is_derived(struct type *t, struct type *base)
{
	struct member *m;

	if (!t || !base || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return false;
	if (t == base)
		return true;
	for (m = t->u.structunion.members; m; m = m->next) {
		if (!m->name && cpp_is_derived(m->type, base))
			return true;
	}
	return false;
}

/* Does class `t` (tag `tag`) define a constructor?  A constructor is a
 * function member whose name equals the class tag (lowered to
 * `Class_Class`). */
bool
cpp_has_ctor(struct type *t, const char *tag)
{
	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION) || !tag)
		return false;
	return cpp_is_member_function(t, tag);
}

/* Emit a call to the default constructor of class-typed local `d` after
 * its storage is laid out (`Counter c;` -> `Counter_Class(&c)`).  No-op
 * for non-class types, classes without a constructor, or global objects
 * (static init is a later stage).  Returns whether a call was emitted. */
bool
cpp_emit_default_ctor(struct func *f, struct decl *d)
{
	extern struct value *funcexpr(struct func *, struct expr *);
	extern struct scope filescope;

	struct type *t = d->type;
	const char *tag;
	char mname[256];
	struct decl *fd;
	struct expr *fn, *obj, *call;

	if (!f || !d || d->u.obj.storage != SDAUTO)
		return false;
	tag = t ? t->u.structunion.tag : NULL;
	if (!tag)
		return false;

	obj = mkexpr(EXPRIDENT, d->type, NULL);
	obj->qual = d->qual;
	obj->lvalue = true;
	obj->u.ident.decl = d;
	obj = mkunaryexpr(TBAND, obj); /* &obj */

	/* A class with no user constructor of its own still needs its base
	 * subobjects constructed and its vptrs installed. */
	if (!cpp_has_ctor(t, tag)) {
		bool any = false;
		if (has_base(t)) {
			emit_base_ctors_for(f, t, obj);
			any = true;
		}
		if (t->u.structunion.poly) {
			cpp_init_vptrs(f, t, obj);
			any = true;
		}
		return any;
	}

	snprintf(mname, sizeof mname, "%s_%s", tag, tag);
	fd = scopegetdecl(t->scope ? t->scope : &filescope, mname, true);
	if (!fd || fd->kind != DECLFUNC)
		return false;

	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn); /* &Class_Class */

	call = mkexpr(EXPRCALL, &typevoid, fn);
	call->u.call.args = obj;
	call->u.call.nargs = 1;
	funcexpr(f, call);
	return true;
}

/* Emit calls to the user default constructors of the direct base
 * subobjects of `classt` (each at its layout offset from `thisp`).
 * A base/member with no user constructor still needs its own bases and
 * data members constructed, so the walk recurses (the vptrs of the whole
 * object are installed afterwards by cpp_init_vptrs). */
static void
emit_base_ctors_for(struct func *f, struct type *classt, struct expr *thisp)
{
	extern struct value *funcexpr(struct func *, struct expr *);
	extern struct scope filescope;

	struct member *m;
	struct type *bt;
	char mname[256];
	struct decl *fd;
	struct expr *fn, *call, *call_this;

	for (m = classt->u.structunion.members; m; m = m->next) {
		/* destructor marker members are not objects */
		if (m->name && m->name[0] == '~')
			continue;
		bt = m->type;
		if (!bt || (bt->kind != TYPESTRUCT && bt->kind != TYPEUNION)) {
			/* scalar member: no ctor call, but a ctor initializer-list
			 * item `: x(v)` must land the value — emit
			 * `*(this + offset) = v`.  Previously the item was dropped
			 * by the `continue`, leaving the scalar member unwritten
			 * (D4: by-value returns of such classes were garbage). */
			if (m->name && bt && bt->kind != TYPEFUNC) {
				struct cpp_init_item *it;
				for (it = g_cpp_init_items; it; it = it->next)
					if (strcmp(it->name, m->name) == 0)
						break;
				if (it && it->args) {
					struct expr *base = mkbinaryexpr(&tok.loc,
					    TADD, exprconvert(thisp, &typeulong),
					    mkconstexpr(&typeulong, m->offset));
					base->type = mkpointertype(bt, QUALNONE);
					struct expr *dst = mkunaryexpr(TMUL, base);
					dst->type = bt;
					dst->lvalue = true;
					funcexpr(f, mkassignexpr(dst, it->args));
				}
			}
			continue;
		}
		if (m->name && m->type && m->type->kind == TYPEFUNC)
			continue; /* member functions occupy no storage */
		if (m->offset) {
			/* this + subobject offset points at this base's subobject
			 * (or class-type member object), always relative to the
			 * original `this` */
			call_this = mkbinaryexpr(&tok.loc, TADD,
			    exprconvert(thisp, &typeulong),
			    mkconstexpr(&typeulong, m->offset));
			call_this->type = mkpointertype(bt, QUALNONE);
		} else {
			call_this = thisp;
		}
		/* A ctor-init-list item names this base/member: `Base(v)` /
		 * `m(v)`.  It may select a constructor overload, so resolve the
		 * mangled name from the argument expression types (like an
		 * object declaration `Point p(3)`).  A direct base is registered
		 * as an anonymous member, so its class tag is the name used in
		 * the init list. */
		{
			const char *key = m->name ? m->name :
			    (bt->u.structunion.tag ? bt->u.structunion.tag : NULL);
			struct cpp_init_item *match = NULL;
			if (key) {
				struct cpp_init_item *it;
				for (it = g_cpp_init_items; it; it = it->next)
					if (strcmp(it->name, key) == 0) {
						match = it;
						break;
					}
			}
			if (match && match->args) {
				/* explicit initializer: select the matching overload */
				char code[256];
				cpp_mangled_name_args(bt, bt->u.structunion.tag,
				    match->args, code, sizeof code, true);
				fd = scopegetdecl(bt->scope ? bt->scope : &filescope,
				    code, true);
				if (!fd || fd->kind != DECLFUNC) {
					cpp_mangled_name_args(bt, bt->u.structunion.tag,
					    match->args, code, sizeof code, false);
					fd = scopegetdecl(bt->scope ? bt->scope : &filescope,
					    code, true);
				}
				if (!fd || fd->kind != DECLFUNC)
					error_code(E_DECL, &tok.loc, "no matching constructor for '%s' in initializer list", key);
			} else {
				/* no explicit initializer: default construction */
				if (!bt->u.structunion.tag ||
				    !cpp_has_ctor(bt, bt->u.structunion.tag)) {
					if (has_base(bt))
						emit_base_ctors_for(f, bt, call_this);
					continue;
				}
				snprintf(mname, sizeof mname, "%s_%s",
				    bt->u.structunion.tag, bt->u.structunion.tag);
				fd = scopegetdecl(bt->scope ? bt->scope : &filescope,
				    mname, true);
				if (!fd || fd->kind != DECLFUNC)
					continue;
			}
			fn = mkexpr(EXPRIDENT, fd->type, NULL);
			fn->u.ident.decl = fd;
			fn = decay(fn); /* &Class_Class */
			call = mkexpr(EXPRCALL, &typevoid, fn);
			call->u.call.args = call_this;
			call->u.call.nargs = 1;
			if (match && match->args) {
				/* reference parameters (copy/move ctors) receive
				 * the address of the argument */
				struct decl *p = fd->type->u.func.params ?
				    fd->type->u.func.params->next : NULL;
				struct expr *a, **end = &call_this->next;
				for (a = match->args; a; a = a->next, p = p ? p->next : NULL) {
					struct expr *arg = a;
					if (p && p->type && p->type->isref)
						arg = mkunaryexpr(TBAND, a);
					*end = arg;
					end = &arg->next;
					++call->u.call.nargs;
				}
			}
			funcexpr(f, call);
		}
	}
}

/* Emit implicit base-class construction at the start of a derived-class
 * constructor: `class D : B { D() {...} }` first runs `B_B(&this)` for
 * each direct base with a user default constructor. */
static void
cpp_emit_base_ctor(struct func *f)
{
	emit_base_ctors_for(f, g_cpp_method.class_type, cpp_this_expr());
}

/* Parse a constructor initializer list `: Base(v), m(v * 2)` that sits
 * between the ctor's parameter list and its body.  Each item is a bare
 * name (base-class tag or data member) followed by parenthesized
 * argument expressions; the arguments are parsed as expressions and
 * kept for emit_base_ctors_for.  The trailing '{' of the body is left
 * for stmt() to consume. */
static void
cpp_parse_init_list(struct func *f, struct scope *fs)
{
	extern struct expr *condexpr(struct scope *);
	struct cpp_init_item *it;

	if (tok.kind != TCOLON) {
		return;
	}
	next(); /* consume ':' */
	for (;;) {
		if (tok.kind < TIDENT)
			error_code(E_DECL, &tok.loc, "expected member or base name in ctor initializer list");
		it = xmalloc(sizeof *it);
		it->name = tokenstr(tok.kind);
		it->args = NULL;
		it->next = NULL;
		next();
		expect(TLPAREN, "'(' after initializer name");
		{
			/* one or more comma-separated argument expressions (parsed
			 * with condexpr so the ',' between init items is not eaten);
			 * an empty argument list (`Base()`) is valid too */
			struct expr *head = NULL, **ae = &head;
			for (;;) {
				struct expr *a;
				if (tok.kind == TRPAREN)
					break;
				a = condexpr(fs);
				*ae = a;
				ae = &a->next;
				if (tok.kind != TCOMMA)
					break;
				next(); /* consume ',' between args */
			}
			it->args = head;
		}
		expect(TRPAREN, "')' after initializer arguments");
		*g_cpp_init_end = it;
		g_cpp_init_end = &it->next;
		if (tok.kind == TCOMMA) {
			next();
			continue;
		}
		break;
	}
}

/* Emit calls to the user destructors of the base subobjects of `classt`,
 * each at its layout offset from `thisp`.  Called at the end of a
 * derived-class destructor body so destruction runs in reverse
 * construction order (derived body → each base's own `Base_dtor`, which
 * recurses into its bases).  A base with no user destructor still needs
 * its own bases destroyed, so the walk recurses. */
static void
emit_base_dtors_for(struct func *f, struct type *classt, struct expr *thisp)
{
	extern struct value *funcexpr(struct func *, struct expr *);
	extern struct scope filescope;

	struct member *m;
	struct type *bt;
	char mname[256];
	struct decl *fd;
	struct expr *fn, *call, *call_this;

	for (m = classt->u.structunion.members; m; m = m->next) {
		/* destructor marker members are not objects */
		if (m->name && m->name[0] == '~')
			continue;
		bt = m->type;
		if (!bt || (bt->kind != TYPESTRUCT && bt->kind != TYPEUNION))
			continue;
		if (m->name && m->type && m->type->kind == TYPEFUNC)
			continue; /* member functions occupy no storage */
		if (m->offset) {
			/* this + subobject offset points at this base's subobject
			 * (or class-type member object), always relative to the
			 * original `this` */
			call_this = mkbinaryexpr(&tok.loc, TADD,
			    exprconvert(thisp, &typeulong),
			    mkconstexpr(&typeulong, m->offset));
			call_this->type = mkpointertype(bt, QUALNONE);
		} else {
			call_this = thisp;
		}
		if (!bt->u.structunion.tag || !cpp_has_dtor(bt)) {
			/* no user destructor: still destroy its own bases */
			if (has_base(bt))
				emit_base_dtors_for(f, bt, call_this);
			continue;
		}
		snprintf(mname, sizeof mname, "%s_dtor", bt->u.structunion.tag);
		fd = scopegetdecl(bt->scope ? bt->scope : &filescope, mname, true);
		if (!fd || fd->kind != DECLFUNC)
			continue;
		fn = mkexpr(EXPRIDENT, fd->type, NULL);
		fn->u.ident.decl = fd;
		fn = decay(fn); /* &Base_dtor */
		call = mkexpr(EXPRCALL, &typevoid, fn);
		call->u.call.args = call_this;
		call->u.call.nargs = 1;
		funcexpr(f, call);
	}
}

/* Emit implicit base-class destruction at the end of a derived-class
 * destructor: `class D : B { ~D() {...} }` runs each direct base's
 * `B_dtor(&this)` after the derived body. */
static void
cpp_emit_base_dtor(struct func *f)
{
	emit_base_dtors_for(f, g_cpp_method.class_type, cpp_this_expr());
}

/* Does `t` have at least one direct base class (anonymous subobject)? */
static bool
has_base(struct type *t)
{
	struct member *m;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return false;
	for (m = t->u.structunion.members; m; m = m->next)
		if (!m->name)
			return true;
	return false;
}

/* Does class `t` define a destructor?  Destructors are registered under a
 * `~Class` marker member name (see structdecl's TILDE branch). */
bool
cpp_has_dtor(struct type *t)
{
	struct member *m;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return false;
	for (m = t->u.structunion.members; m; m = m->next)
		if (m->name && m->name[0] == '~' &&
		    m->type && m->type->kind == TYPEFUNC)
			return true;
	return false;
}

/* Emit a call to the destructor of local class-typed `d` at end of scope
 * (`Class_dtor(&obj)`).  No-op for non-class types or classes without a
 * destructor.  Returns whether a call was emitted. */
bool
cpp_emit_dtor(struct func *f, struct decl *d)
{
	extern struct value *funcexpr(struct func *, struct expr *);
	extern struct scope filescope;

	struct type *t = d->type;
	const char *tag;
	char mname[256];
	struct decl *fd;
	struct expr *fn, *obj, *call;

	if (!f || !d || d->u.obj.storage != SDAUTO)
		return false;
	tag = t ? t->u.structunion.tag : NULL;
	if (!tag || !cpp_has_dtor(t))
		return false;
	snprintf(mname, sizeof mname, "%s_dtor", tag);
	fd = scopegetdecl(t->scope ? t->scope : &filescope, mname, true);
	if (!fd || fd->kind != DECLFUNC)
		return false;

	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn); /* &Class_dtor */

	obj = mkexpr(EXPRIDENT, d->type, NULL);
	obj->qual = d->qual;
	obj->lvalue = true;
	obj->u.ident.decl = d;
	obj = mkunaryexpr(TBAND, obj); /* &obj */

	call = mkexpr(EXPRCALL, &typevoid, fn);
	call->u.call.args = obj;
	call->u.call.nargs = 1;
	funcexpr(f, call);
	d->dtor_done = true;
	return true;
}

/* Emit a destructor call for a global class object (`Class_dtor(&g)`)
 * into __mxx_global_var_fini.  No-op for classes without a destructor. */
static void
cpp_emit_global_dtor(struct func *f, struct decl *d)
{
	extern struct value *funcexpr(struct func *, struct expr *);
	extern struct scope filescope;

	struct type *t = d->type;
	const char *tag;
	char mname[256];
	struct decl *fd;
	struct expr *fn, *obj, *call;

	if (!f || !d)
		return;
	tag = t ? t->u.structunion.tag : NULL;
	if (!tag || !cpp_has_dtor(t))
		return;
	snprintf(mname, sizeof mname, "%s_dtor", tag);
	fd = scopegetdecl(t->scope ? t->scope : &filescope, mname, true);
	if (!fd || fd->kind != DECLFUNC)
		return;

	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn); /* &Class_dtor */

	obj = mkexpr(EXPRIDENT, d->type, NULL);
	obj->qual = d->qual;
	obj->lvalue = true;
	obj->u.ident.decl = d;
	obj = mkunaryexpr(TBAND, obj); /* &g */

	call = mkexpr(EXPRCALL, &typevoid, fn);
	call->u.call.args = obj;
	call->u.call.nargs = 1;
	funcexpr(f, call);
}

/* Emit destructor calls for all local class objects declared in scope `s`
 * when a block exits (reverse declaration order not yet implemented).
 * Called by stmt()'s compound-statement branch before delscope. */
void
cpp_emit_scope_dtors(struct func *f, struct scope *s)
{
	struct decl *d;

	if (!f || !s)
		return;
	/* objects is head-inserted in declaration order, so walking it
	 * front-to-back destroys in reverse declaration order (C++). */
	for (d = s->objects; d; d = d->next)
		if (!d->dtor_done)
			cpp_emit_dtor(f, d);
}

/* Emit a constructor call with explicit arguments for `Point p(3, 4);`
 * (`Point_Point(&p, 3, 4)`).  `args` is the expression list collected by
 * declarator(); the this address is prepended. */
void
cpp_emit_ctor_call(struct func *f, struct decl *d, struct expr *args)
{
	extern struct value *funcexpr(struct func *, struct expr *);
	extern struct scope filescope;

	struct type *t = d->type;
	const char *tag;
	char mname[256];
	struct decl *fd, *p;
	struct expr *fn, *obj, *call, *a, **end;
	size_t n = 0;

	if (!f || !d)
		return;
	tag = t ? t->u.structunion.tag : NULL;
	if (!tag || !cpp_has_ctor(t, tag)) {
		/* aggregate (no user ctor): direct-initialize the data members
		 * positionally — `P p(1, 2)` lowers to `p.a = 1; p.b = 2;` */
		struct expr *obj;
		struct member *m;
		struct expr *a;
		obj = mkexpr(EXPRIDENT, d->type, NULL);
		obj->qual = d->qual;
		obj->lvalue = true;
		obj->u.ident.decl = d;
		obj = mkunaryexpr(TBAND, obj); /* &p */
		for (m = t ? t->u.structunion.members : NULL, a = args;
		     m && a; m = m->next, a = a->next) {
			struct expr *base, *dst;
			if (m->name && m->name[0] == '~')
				continue;
			if (m->type && m->type->kind == TYPEFUNC)
				continue;
			base = mkbinaryexpr(&tok.loc, TADD,
			    exprconvert(obj, &typeulong),
			    mkconstexpr(&typeulong, m->offset));
			base->type = mkpointertype(m->type, QUALNONE);
			dst = mkunaryexpr(TMUL, base);
			dst->type = m->type;
			dst->lvalue = true;
			funcexpr(f, mkassignexpr(dst, a));
		}
		return;
	}
	/* overload resolution: append the encoded argument types (reference
	 * binding first for lvalue args, falling back to by-value) */
	{
		char code[256];
		cpp_mangled_name_args(t, tag, args, code, sizeof code, true);
		snprintf(mname, sizeof mname, "%s", code);
	}
	fd = scopegetdecl(t->scope ? t->scope : &filescope, mname, true);
	if (!fd || fd->kind != DECLFUNC) {
		char code2[256];
		cpp_mangled_name_args(t, tag, args, code2, sizeof code2, false);
		fd = scopegetdecl(t->scope ? t->scope : &filescope, code2, true);
		if (fd && fd->kind == DECLFUNC)
			snprintf(mname, sizeof mname, "%s", code2);
	}
	if (!fd || fd->kind != DECLFUNC) {
		error_code(E_DECL, &tok.loc, "no matching constructor for object '%s'", d->name);
		return;
	}

	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn); /* &Point_Point */

	obj = mkexpr(EXPRIDENT, d->type, NULL);
	obj->qual = d->qual;
	obj->lvalue = true;
	obj->u.ident.decl = d;
	obj = mkunaryexpr(TBAND, obj); /* &p */

	call = mkexpr(EXPRCALL, &typevoid, fn);
	call->u.call.args = obj;
	call->u.call.nargs = 1;
	end = &obj->next;
	/* reference parameters (copy/move ctors: `Vec(Vec &o)`, `Vec(Vec &&o)`)
	 * receive the address of the argument, like member-function calls;
	 * by-value parameters receive the value. */
	for (a = args, p = fd->type->u.func.params ? fd->type->u.func.params->next : NULL;
	     a; a = a->next, p = p ? p->next : NULL) {
		struct expr *arg = a;
		if (p && p->type && p->type->isref)
			arg = mkunaryexpr(TBAND, a);
		*end = arg;
		end = &arg->next;
		++call->u.call.nargs;
	}
	(void)n;
	funcexpr(f, call);
}


/* Append the mangling code for one type into buf (NUL-terminated). */
static void
cpp_mangle_type(struct type *t, char *buf, size_t bufsz)
{
	char *p = buf;
	char *end = buf + bufsz - 1;

	if (!t) {
		*p++ = 'v';
		goto out;
	}
	/* C++ references mangle with a distinct marker so `f(Vec)`, `f(Vec &)`
	 * and `f(Vec &&)` get different overload names (the caller binds an
	 * object by address, but the types must not collide): 'R' = lvalue
	 * reference, 'V' = rvalue reference (move overloads). */
	if (t->isref && t->kind == TYPEPOINTER) {
		if (p + 1 <= end)
			*p++ = t->isrref ? 'V' : 'R';
		t = t->base;
	}
	switch (t->kind) {
	case TYPEVOID:     *p++ = 'v'; break;
	case TYPEBOOL:     *p++ = 'b'; break;
	case TYPECHAR:     *p++ = t->u.arith.issigned ? 'c' : 'C'; break;
	case TYPECHAR8:    *p++ = 'D'; break;   /* Itanium ABI for char8_t */
	case TYPESHORT:    *p++ = t->u.arith.issigned ? 's' : 'S'; break;
	case TYPEINT:      *p++ = t->u.arith.issigned ? 'i' : 'u'; break;
	case TYPELONG:     *p++ = t->u.arith.issigned ? 'l' : 'L'; break;
	case TYPELLONG:    *p++ = t->u.arith.issigned ? 'j' : 'J'; break;
	case TYPEFLOAT:    *p++ = 'f'; break;
	case TYPEDOUBLE:   *p++ = 'd'; break;
	case TYPELDOUBLE:  *p++ = 'e'; break;
	case TYPEPOINTER:  *p++ = 'p'; break;
	case TYPEENUM:     *p++ = 'E'; break;
	case TYPEARRAY:    *p++ = 'A'; break;
	case TYPENULLPTR:  *p++ = 'n'; break;
	case TYPESTRUCT:
	case TYPEUNION:
		*p++ = 'o';
		if (t->u.structunion.tag) {
			size_t n = strlen(t->u.structunion.tag);
			if (p + n <= end) {
				memcpy(p, t->u.structunion.tag, n);
				p += n;
			}
		}
		break;
	default:           *p++ = 'x'; break;
	}
out:
	*p = '\0';
}

/* Mangled name of method `name` of class `t`, with the given argument
 * expressions' types appended for overload resolution:
 * `Class_method_ii` etc.  Returns the name in buf.
 *
 * `prefer_ref` marks lvalue arguments as bindable by reference ('R'
 * prefix), matching C++'s preference for reference overloads on
 * lvalues; the caller falls back to the plain (by-value) encoding when
 * no reference overload exists. */
void
cpp_mangled_name_args(struct type *t, const char *name, struct expr *args,
                      char *buf, size_t bufsz, bool prefer_ref)
{
	struct type *owner = NULL;
	size_t n;

	if (cpp_method_member(t, name, &owner) && owner)
		t = owner;
	snprintf(buf, bufsz, "%s_%s",
	         (t && t->u.structunion.tag) ? t->u.structunion.tag : "anon",
	         name);
	n = strlen(buf);
	for (; args; args = args->next) {
		char code[64];
		size_t cl;
		/* overload-resolution value category: lvalue args prefer the
		 * lvalue-reference overload ('R'), rvalue (temporary) args prefer
		 * the rvalue-reference/move overload ('V') */
		if (prefer_ref && n + 1 < bufsz)
			buf[n++] = args->lvalue ? 'R' : 'V';
		cpp_mangle_type(args->type, code, sizeof code);
		cl = strlen(code);
		if (n + cl < bufsz) {
			memcpy(buf + n, code, cl + 1);
			n += cl;
		}
	}
}

/* --- free-function overloading (file/namespace scope) --------------- */

/* Mangled name of a file-scope (free) function `name` whose function
 * type is `funct`, with the parameter types appended exactly like the
 * member scheme (`helper_ii` for `int helper(int, int)`).  Used at
 * declaration time: a same-name free function with a different
 * signature is registered under this name instead of being rejected as
 * a conflicting redeclaration. */
void
cpp_free_mangle_name(const char *name, struct type *funct, char *buf,
                     size_t bufsz)
{
	struct decl *cur;
	size_t n;

	snprintf(buf, bufsz, "%s_", name);
	n = strlen(buf);
	if (funct && funct->kind == TYPEFUNC) {
		for (cur = funct->u.func.params; cur; cur = cur->next) {
			char code[64];
			size_t cl;
			cpp_mangle_type(cur->type, code, sizeof code);
			cl = strlen(code);
			if (n + cl < bufsz) {
				memcpy(buf + n, code, cl + 1);
				n += cl;
			}
		}
	}
}

/* Mangled name of a free-function call `name(args...)`, encoded from
 * the argument expression types for overload resolution (`helper_ii`).
 * `prefer_ref` marks the value category of each argument — lvalue with
 * 'R', rvalue (temporary) with 'V' — so `f(Vec&)` wins on lvalues and
 * `f(Vec&&)` (the move overload) wins on temporaries, falling back to
 * the by-value overload when no reference variant exists. */
void
cpp_free_mangle_name_args(const char *name, struct expr *args, char *buf,
                          size_t bufsz, bool prefer_ref)
{
	size_t n;

	snprintf(buf, bufsz, "%s_", name);
	n = strlen(buf);
	for (; args; args = args->next) {
		char code[64];
		size_t cl;
		if (prefer_ref && n + 1 < bufsz)
			buf[n++] = args->lvalue ? 'R' : 'V';
		cpp_mangle_type(args->type, code, sizeof code);
		cl = strlen(code);
		if (n + cl < bufsz) {
			memcpy(buf + n, code, cl + 1);
			n += cl;
		}
	}
}

const char *
cpp_mangled_name(struct type *t, const char *name, char *buf, size_t bufsz)
{
	struct type *owner = NULL;

	/* inherited methods mangle under the defining base class, so
	 * `d.base_method()` resolves to `Base_base_method` */
	if (cpp_method_member(t, name, &owner) && owner)
		t = owner;
	snprintf(buf, bufsz, "%s_%s",
	         (t && t->u.structunion.tag) ? t->u.structunion.tag : "anon",
	         name);
	return buf;
}

/* --- virtual functions / vtable (C.2.5) ------------------------------ */

/* Signature key of a virtual method: `name` + trailing-const marker + the
 * encoded parameter types, independent of the declaring class so an
 * override in a derived class reuses the base slot.  Mirrors the mangled
 * name encoding in cpp_define_method (`Class_methodK<params>`). */
static void
cpp_vkey(const char *mname, struct type *funct, bool is_const,
         char *buf, size_t bufsz)
{
	struct decl *cur;
	size_t n;

	snprintf(buf, bufsz, "%s%s", mname ? mname : "?", is_const ? "K" : "");
	n = strlen(buf);
	if (funct && funct->kind == TYPEFUNC) {
		for (cur = funct->u.func.params; cur; cur = cur->next) {
			char code[64];
			size_t cl;
			cpp_mangle_type(cur->type, code, sizeof code);
			cl = strlen(code);
			if (n + cl < bufsz) {
				memcpy(buf + n, code, cl + 1);
				n += cl;
			}
		}
	}
}

/* Symbol name of the implementation of virtual member `m` of `owner`
 * (`Class_methodK<params>`), matching cpp_define_method's mangling. */
static void
cpp_slot_mangled(struct type *owner, struct member *m,
                 char *buf, size_t bufsz)
{
	struct decl *cur;
	size_t n;

	snprintf(buf, bufsz, "%s_%s",
	         (owner && owner->u.structunion.tag) ? owner->u.structunion.tag
	                                            : "anon",
	         m->name);
	n = strlen(buf);
	if (m->is_const && n + 1 < bufsz) {
		strcpy(buf + n, "K");
		++n;
	}
	if (m->type && m->type->kind == TYPEFUNC) {
		for (cur = m->type->u.func.params; cur; cur = cur->next) {
			char code[64];
			size_t cl;
			cpp_mangle_type(cur->type, code, sizeof code);
			cl = strlen(code);
			if (n + cl < bufsz) {
				memcpy(buf + n, code, cl + 1);
				n += cl;
			}
		}
	}
}

/* Find the most-derived implementation of the virtual method with `key`
 * in the class hierarchy rooted at `d` (the class itself first, then its
 * direct bases in declaration order).  Returns true and sets *owner/*outm
 * on success. */
static bool
cpp_find_final(struct type *d, const char *key, struct type **owner,
               struct member **outm)
{
	struct member *m;
	char k[256];

	if (!d || (d->kind != TYPESTRUCT && d->kind != TYPEUNION))
		return false;
	for (m = d->u.structunion.members; m; m = m->next) {
		if (m->is_virtual && m->name) {
			cpp_vkey(m->name, m->type, m->is_const, k, sizeof k);
			if (strcmp(k, key) == 0) {
				if (owner)
					*owner = d;
				if (outm)
					*outm = m;
				return true;
			}
		}
	}
	for (m = d->u.structunion.members; m; m = m->next)
		if (!m->name && cpp_find_final(m->type, key, owner, outm))
			return true;
	return false;
}

/* Class that declares the member function `name` of `t` (the defining
 * base for inherited methods), or NULL. */
struct type *
cpp_method_owner(struct type *t, const char *name)
{
	struct type *owner = NULL;

	if (cpp_method_member(t, name, &owner))
		return owner;
	return NULL;
}

bool
cpp_is_virtual(struct type *t, const char *name)
{
	struct member *m = cpp_method_member(t, name, NULL);

	return m && m->is_virtual;
}

/* Recursive helper for cpp_base_offset. */
static bool
cpp_base_offset_r(struct type *d, struct type *base, unsigned long long *off)
{
	struct member *m;
	unsigned long long sub;

	if (d == base) {
		*off = 0;
		return true;
	}
	if (!d || (d->kind != TYPESTRUCT && d->kind != TYPEUNION))
		return false;
	for (m = d->u.structunion.members; m; m = m->next) {
		if (!m->name && m->type &&
		    (m->type->kind == TYPESTRUCT || m->type->kind == TYPEUNION)) {
			if (m->type == base) {
				*off = m->offset;
				return true;
			}
			if (cpp_base_offset_r(m->type, base, &sub)) {
				*off = m->offset + sub;
				return true;
			}
		}
	}
	return false;
}

unsigned long long
cpp_base_offset(struct type *derived, struct type *base)
{
	unsigned long long off = 0;

	cpp_base_offset_r(derived, base, &off);
	return off;
}

int
cpp_vslot_index(struct type *t, struct member *m)
{
	(void)t;
	return m ? m->vslot : -1;
}

/* Insert the hidden vptr member at offset 0, shifting every existing
 * member (including base subobjects) up by one pointer.  Only called for
 * classes that need their own vptr (polymorphic with no polymorphic
 * primary base). */
static void
cpp_insert_vptr(struct type *t)
{
	struct member *v, *m;

	v = xmalloc(sizeof *v);
	v->name = xmalloc(7);
	strcpy(v->name, "__vptr");
	v->type = mkpointertype(&typevoid, QUALNONE);
	v->qual = QUALNONE;
	v->offset = 0;
	v->bits.before = v->bits.after = 0;
	v->access = ACC_PRIVATE;
	v->is_mutable = false;
	v->is_virtual = false;
	v->is_const = false;
	v->vslot = -1;
	v->next = t->u.structunion.members;
	for (m = v->next; m; m = m->next)
		/* function members occupy no layout space (their offset is the
		 * defining subobject's offset, not a byte position), so only
		 * data members and base subobjects shift up */
		if (!(m->type && m->type->kind == TYPEFUNC))
			m->offset += 8;
	t->u.structunion.members = v;
	t->size += 8;
	if (t->align < 8)
		t->align = 8;
}

/* Polyorphic classes needing vtable emission, in definition order. */
struct cpp_vclass {
	struct type *t;
	struct cpp_vclass *next;
};
static struct cpp_vclass *g_cpp_vclasses;
static struct cpp_vclass **g_cpp_vclasses_end = &g_cpp_vclasses;

/* Compute the vtable slot layout for `t` (called once the class body and
 * all base classes are complete): primary-base slots first, then this
 * class's own new virtuals; overrides reuse the base slot index.  Also
 * inserts the hidden vptr member when needed and records the class for
 * vtable emission. */
static void
cpp_compute_vtable(struct type *t)
{
	struct member *m;
	struct type *P = NULL;
	struct cpp_vslot *vs, *bvs, *nv, **ve;
	bool has_poly_base = false;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return;

	/* primary base: first polymorphic direct base (anonymous member) */
	for (m = t->u.structunion.members; m; m = m->next) {
		if (!m->name && m->type &&
		    (m->type->kind == TYPESTRUCT || m->type->kind == TYPEUNION) &&
		    m->type->u.structunion.poly) {
			P = m->type;
			break;
		}
	}
	t->u.structunion.primary_base = P;

	/* layout: primary base's slots, then own virtuals (overrides reuse
	 * the base slot instead of appending) */
	t->u.structunion.vslots = NULL;
	t->u.structunion.nvslots = 0;
	for (bvs = P ? P->u.structunion.vslots : NULL; bvs; bvs = bvs->next) {
		nv = xmalloc(sizeof *nv);
		nv->name = bvs->name;
		memcpy(nv->key, bvs->key, sizeof nv->key);
		nv->m = NULL;
		nv->owner = bvs->owner;
		nv->index = t->u.structunion.nvslots++;
		nv->next = NULL;
		ve = &t->u.structunion.vslots;
		while (*ve)
			ve = &(*ve)->next;
		*ve = nv;
	}
	for (vs = t->u.structunion.own_virtuals; vs; vs = vs->next) {
		int idx = -1;
		for (bvs = t->u.structunion.vslots; bvs; bvs = bvs->next)
			if (strcmp(bvs->key, vs->key) == 0) {
				idx = bvs->index;
				break;
			}
		vs->index = idx;
		if (idx < 0) {
			/* genuinely new virtual: append a slot */
			vs->index = t->u.structunion.nvslots++;
			nv = xmalloc(sizeof *nv);
			nv->name = vs->name;
			memcpy(nv->key, vs->key, sizeof nv->key);
			nv->m = vs->m;
			nv->owner = t;
			nv->index = vs->index;
			nv->next = NULL;
			ve = &t->u.structunion.vslots;
			while (*ve)
				ve = &(*ve)->next;
			*ve = nv;
		}
	}

	/* record the slot index on each virtual member (the call lowering
	 * reads it via m->vslot) */
	for (m = t->u.structunion.members; m; m = m->next) {
		if (m->is_virtual && m->name) {
			char key[256];
			cpp_vkey(m->name, m->type, m->is_const, key, sizeof key);
			for (vs = t->u.structunion.own_virtuals; vs; vs = vs->next)
				if (strcmp(vs->key, key) == 0) {
					m->vslot = vs->index;
					break;
				}
		}
	}

	for (m = t->u.structunion.members; m; m = m->next)
		if (!m->name && m->type &&
		    (m->type->kind == TYPESTRUCT || m->type->kind == TYPEUNION) &&
		    m->type->u.structunion.poly)
			has_poly_base = true;
	t->u.structunion.poly = t->u.structunion.own_poly || has_poly_base;

	if (t->u.structunion.poly && !P && t->u.structunion.own_poly)
		cpp_insert_vptr(t);

	/* record for vtable emission (once) */
	if (t->u.structunion.poly) {
		struct cpp_vclass *vc;
		for (vc = g_cpp_vclasses; vc; vc = vc->next)
			if (vc->t == t)
				return;
		vc = xmalloc(sizeof *vc);
		vc->t = t;
		vc->next = NULL;
		*g_cpp_vclasses_end = vc;
		g_cpp_vclasses_end = &vc->next;
	}
}

/* Vtable symbol name for class `most`, viewing the slot layout of
 * `owner`: `__mxx_vtable_<most>` (primary) or
 * `__mxx_vtable_<most>_<owner>` (secondary subobject). */
static void
vt_symbol_name(struct type *most, struct type *owner, char *buf, size_t bufsz)
{
	const char *mtag, *otag;

	mtag = most->u.structunion.tag ? most->u.structunion.tag : "anon";
	if (owner == most) {
		snprintf(buf, bufsz, "__mxx_vtable_%s", mtag);
		return;
	}
	otag = owner->u.structunion.tag ? owner->u.structunion.tag : "anon";
	snprintf(buf, bufsz, "__mxx_vtable_%s_%s", mtag, otag);
}

/* A decl whose address is the named vtable symbol (used by the vptr
 * initialization stores). */
static struct decl *
vt_decl(struct type *most, struct type *owner)
{
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);
	char name[256];
	char *pname;
	struct decl *vd;

	vt_symbol_name(most, owner, name, sizeof name);
	pname = xmalloc(strlen(name) + 1);
	strcpy(pname, name);
	vd = mkdecl(pname, DECLOBJECT, mkpointertype(&typevoid, QUALNONE),
	            QUALNONE, LINKEXTERN);
	vd->value = mkglobal(vd);
	return vd;
}

/* Emit `*(void **)((char *)thisp + off) = &vtable` — install one vptr.
 * Built directly at the IR level (IADD + ISTOREL) to keep the frontend
 * function body simple. */
/* IR opcode numbers used by the vptr initialization (enum instkind in
 * irgen.h; values verified against include/ops.h). */
enum {
	CPP_IR_IADD = 1,
	CPP_IR_ISTOREL = 17,
};

/* Emit `*(void **)((char *)thisp + off) = &vtable` — install one vptr.
 * Built directly at the IR level (IADD + ISTOREL) to keep the frontend
 * function body simple. */
static void
emit_vptr_store(struct func *f, struct expr *thisp, unsigned long long off,
                struct decl *vd)
{
	extern struct value *funcexpr(struct func *, struct expr *);
	extern struct value *funcinst(struct func *, int, int,
	    struct value *, struct value *);
	extern struct value *mkintconst(unsigned long long);
	struct value *base;
	int pcls = typelong.size == 4 ? 'w' : 'l';

	base = funcexpr(f, thisp);
	if (off)
		base = funcinst(f, CPP_IR_IADD, pcls, base, mkintconst(off));
	/* *base = &__mxx_vtable_... (VALUE_GLOBAL address) */
	funcinst(f, CPP_IR_ISTOREL, 0, vd->value, base);
}

/* Install every vptr of a complete object of type `t` at `thisp`:
 * the primary vptr (offset 0) plus one per secondary polymorphic base
 * subobject.  Called at the start of a constructor body (after the base
 * constructors ran) and when an object with no user constructor is
 * defined. */
static void
cpp_init_vptrs(struct func *f, struct type *t, struct expr *thisp)
{
	struct member *m;

	if (!f || !t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return;
	if (!t->u.structunion.poly)
		return;
	if (t->u.structunion.primary_base || t->u.structunion.own_poly)
		emit_vptr_store(f, thisp, 0, vt_decl(t, t));
	for (m = t->u.structunion.members; m; m = m->next) {
		if (!m->name && m->type &&
		    (m->type->kind == TYPESTRUCT || m->type->kind == TYPEUNION) &&
		    m->type->u.structunion.poly &&
		    m->type != t->u.structunion.primary_base)
			emit_vptr_store(f, thisp, m->offset, vt_decl(t, m->type));
	}
}

/* Emit one vtable: the slot layout of `owner` filled with the most-derived
 * implementations found in `most`.  The primary table (owner == most) is
 * exported; secondary tables stay local. */
static void
emit_vtable_one(struct type *most, struct type *owner)
{
	char name[256];
	struct cpp_vslot *vs;

	vt_symbol_name(most, owner, name, sizeof name);
	printf(".section .data,\"aw\",@progbits\n");
	printf(".balign 8\n");
	if (owner == most)
		printf(".globl %s\n", name);
	printf("%s:\n", name);
	for (vs = owner->u.structunion.vslots; vs; vs = vs->next) {
		struct type *impl_owner = NULL;
		struct member *impl = NULL;
		if (cpp_find_final(most, vs->key, &impl_owner, &impl)) {
			char mangled[256];
			cpp_slot_mangled(impl_owner, impl, mangled, sizeof mangled);
			printf("    .quad %s\n", mangled);
		} else {
			printf("    .quad 0\n");
		}
	}
}

/* Emit the vtables of every polymorphic class defined in this translation
 * unit: the primary table plus one secondary table per polymorphic
 * secondary base subobject. */
void
cpp_emit_vtables(void)
{
	struct cpp_vclass *vc;
	struct member *m;

	for (vc = g_cpp_vclasses; vc; vc = vc->next) {
		struct type *t = vc->t;
		if (t->u.structunion.primary_base || t->u.structunion.own_poly)
			emit_vtable_one(t, t);
		for (m = t->u.structunion.members; m; m = m->next) {
			if (!m->name && m->type &&
			    (m->type->kind == TYPESTRUCT || m->type->kind == TYPEUNION) &&
			    m->type->u.structunion.poly &&
			    m->type != t->u.structunion.primary_base)
				emit_vtable_one(t, m->type);
		}
	}
}

/* Shallow-deep clone of a simple expression tree.  Used to build the
 * virtual-call indirect callee expression without sharing nodes with the
 * call's this argument (the call tree is freed with delexpr, which
 * assumes a tree, not a DAG). */
static struct expr *
cpp_expr_clone(struct expr *e)
{
	struct expr *n;

	if (!e)
		return NULL;
	n = mkexpr(e->kind, e->type, e->base ? cpp_expr_clone(e->base) : NULL);
	n->qual = e->qual;
	n->lvalue = e->lvalue;
	n->decayed = e->decayed;
	n->op = e->op;
	n->toeval = NULL;
	n->next = NULL;
	switch (e->kind) {
	case EXPRIDENT:
		n->u.ident.decl = e->u.ident.decl;
		break;
	case EXPRCONST:
		n->u.constant = e->u.constant;
		break;
	case EXPRCAST:
		n->toeval = cpp_expr_clone(e->toeval);
		break;
	case EXPRBINARY:
		n->u.binary.l = cpp_expr_clone(e->u.binary.l);
		n->u.binary.r = cpp_expr_clone(e->u.binary.r);
		break;
	default:
		/* the this object is never anything more complex */
		break;
	}
	return n;
}

/* Build the indirect callable for a virtual member call `obj.vmeth(args)`:
 * `*(fn_type **)((*(void **)thisp) + slot * 8)`.  The call lowering
 * (TLPAREN) prepends the this argument and matches the explicit
 * parameters against fn_type. */
struct expr *
cpp_make_vcall(struct expr *thisp, struct type *owner, struct member *m,
               int slot)
{
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);
	struct type *mtype, *fn_type;
	struct decl *td, *cur, *nd, **end;
	struct expr *addr, *vt, *fn;

	if (!owner || !m || !m->type || m->type->kind != TYPEFUNC)
		return NULL;

	mtype = m->type;
	/* signature with the implicit `this` prepended */
	fn_type = mktype(TYPEFUNC, 0);
	fn_type->base = mtype->base;
	fn_type->u.func.isvararg = mtype->u.func.isvararg;
	fn_type->u.func.params = NULL;
	fn_type->u.func.nparam = 0;
	td = mkdecl("this", DECLOBJECT,
	            mkpointertype(owner, m->is_const ? QUALCONST : QUALNONE),
	            QUALNONE, LINKNONE);
	td->u.obj.storage = SDAUTO;
	fn_type->u.func.params = td;
	fn_type->u.func.nparam = 1;
	end = &td->next;
	for (cur = mtype->u.func.params; cur; cur = cur->next) {
		nd = mkdecl(cur->name, DECLOBJECT, cur->type, cur->qual, LINKNONE);
		nd->u.obj.storage = SDAUTO;
		*end = nd;
		end = &nd->next;
		++fn_type->u.func.nparam;
	}

	/* vt = *(void **)thisp  (thisp cloned: the call's this argument is a
	 * separate node; delexpr would otherwise free it twice) */
	addr = mkbinaryexpr(&tok.loc, TADD,
	                    exprconvert(cpp_expr_clone(thisp), &typeulong),
	                    mkconstexpr(&typeulong, 0));
	addr->type = mkpointertype(mkpointertype(&typevoid, QUALNONE), QUALNONE);
	vt = mkunaryexpr(TMUL, addr);
	/* slot = (fn_type **)((unsigned long)vt + slot * 8)
	 * fn    = *slot   (the slot holds the implementation's address) */
	addr = mkbinaryexpr(&tok.loc, TADD, exprconvert(vt, &typeulong),
	                    mkconstexpr(&typeulong, (unsigned long long)slot * 8));
	addr->type = mkpointertype(mkpointertype(fn_type, QUALNONE), QUALNONE);
	fn = mkunaryexpr(TMUL, addr);
	return fn;
}

/* Resolve the address of a member function taken without a call
 * (`&obj.meth`).  The member symbols are argument-encoded
 * (`Class_meth_ii`), so a single overload is found by scanning the
 * class scope for the `Class_meth` prefix.  Returns the unique DECLFUNC
 * decl and copies its exact mangled name into `mname`; returns NULL when
 * there is no such method or more than one candidate (ambiguous). */
struct decl *
cpp_find_unique_member(struct type *t, const char *name,
                       char *mname, size_t mname_sz)
{
	extern struct scope filescope;
	struct scope *sc = t->scope ? t->scope : &filescope;
	const char *tag = t->u.structunion.tag ? t->u.structunion.tag : "anon";
	size_t plen = strlen(tag) + 1 + strlen(name);
	struct decl *found = NULL;
	const char *found_name = NULL;
	int n = 0;
	size_t i;

	snprintf(mname, mname_sz, "%s_%s", tag, name);
	for (i = 0; i < sc->decls.cap; i++) {
		const struct mapkey *k = &sc->decls.keys[i];
		if (!k->str)
			continue;
		if (k->len >= plen && memcmp(k->str, mname, plen) == 0) {
			struct decl *d = sc->decls.vals[i].p;
			if (d && d->kind == DECLFUNC) {
				found = d;
				found_name = k->str;
				++n;
			}
		}
	}
	if (n == 1) {
		snprintf(mname, mname_sz, "%s", found_name);
		return found;
	}
	return NULL;
}

/* --- C.2.8 function templates (instantiate-on-first-use) --------------- */

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
	struct cpp_tmpl_inst *insts;
	struct cpp_tmpl_inst **insts_end;
	struct cpp_tmpl_cls_inst *cls_insts;
	struct cpp_tmpl_cls_inst **cls_insts_end;
	struct cpp_template *next;
};

static struct cpp_template *g_cpp_templates;
static struct cpp_template **g_cpp_templates_end = &g_cpp_templates;
/* Pending template-call names, set by cpp_tmpl_placeholder in primaryexpr
 * and consumed by the TLPAREN lowering after the arguments are known.  A
 * stack so nested template calls in arguments (e.g. f(g(...))) each keep
 * their own pending name. */
static const char *g_cpp_tmpl_stack[64];
static int g_cpp_tmpl_depth;
/* Explicit template arguments parsed after a template name (`f<int, 42>`):
 * a leading type argument list that fills the template parameters before
 * any call-site deduction.  Each slot records whether it is a value (NTTP)
 * or a type.  Cleared after each instantiation. */
static struct type *g_cpp_tmpl_expl_types[16];
static unsigned long long g_cpp_tmpl_expl_vals[16];
static bool g_cpp_tmpl_expl_isval[16];
static int g_cpp_tmpl_expl_n;
/* Parameter-pack element counts of templates being replayed (pushed by the
 * variadic instantiation, popped when the replay completes).  `sizeof...`
 * consults the innermost count. */
static int g_cpp_pack_stack[64];
static int g_cpp_pack_depth;

/* Template parameter type bindings for the instantiation currently being
 * parsed into a constexpr body (set by cpp_tmpl_find_or_instantiate just
 * before decl(), consumed and cleared by cpp_buffer_constexpr_body). */
const char *g_cpp_cexpr_tmpl_params[16];
struct type *g_cpp_cexpr_tmpl_types[16];
unsigned long long g_cpp_cexpr_tmpl_vals[16];
bool g_cpp_cexpr_tmpl_isval[16];
int g_cpp_cexpr_tmpl_n;

/* Number of elements in the innermost replaying parameter pack, for
 * `sizeof...(Args)`. */
int
cpp_sizeof_pack(void)
{
	return g_cpp_pack_depth > 0 ? g_cpp_pack_stack[g_cpp_pack_depth - 1] : 0;
}

/* --- C++11 lambda expressions (anonymous-class lowering) --------------- */

/* Monotonic counter for the synthesized closure class names (`__lambda0`,
 * `__lambda1`, ...). */
static int g_cpp_lambda_count;

const char *
cpp_tmpl_lookup(const char *name)
{
	struct cpp_template *t;

	for (t = g_cpp_templates; t; t = t->next)
		if (strcmp(t->name, name) == 0)
			return t->name;
	return NULL;
}

/* Find a template declaration by name (for explicit-argument parsing that
 * needs the parameter kinds). */
struct cpp_template *
cpp_tmpl_find(const char *name)
{
	struct cpp_template *t;

	for (t = g_cpp_templates; t; t = t->next)
		if (strcmp(t->name, name) == 0)
			return t;
	return NULL;
}

/* C++20 abbreviated function templates:
 *   void f(Integral auto x);             -> template<typename __T0>
 *                                           requires Integral<__T0>
 *                                           void f(__T0 x);
 *   auto g(Integral auto x) -> int;      -> ... requires Integral<__T0>
 *                                           auto g(__T0 x) -> int;
 *   void h(SignedIntegral auto... xs);   -> ... requires SignedIntegral<__T0>
 *                                           void h(__T0... xs);
 *   void k(auto x);                      -> template<typename __T0>
 *                                           void k(__T0 x);   (unconstrained)
 *
 * The whole declaration (through ';' or the closing '}' of a function
 * body) is buffered, rewritten into an equivalent template declaration,
 * and replayed through cpp_template_decl.  Returns true when the
 * declaration was an abbreviated function template (handled); false
 * rewinds the stream untouched so the ordinary declaration parser runs. */
static bool
cpp_try_abbrev_decl(struct scope *s)
{
	struct token *buf = NULL, *wtoks = NULL;
	size_t bn = 0, cap = 0, wn = 0;
	size_t i, pl = (size_t)-1, ple = (size_t)-1;
	int bd = 0, pd = 0, dd = 0; /* brace/paren/bracket depth */
	int nauto = 0, k;
	struct token cur;
	struct token tpl;
	/* 缓冲循环在 ';' / '}' 处 break 前调用了 next()：该 token（声明
	 * 结束后的下一个）已被消费。not_abbrev 回退时必须把它压回，否则
	 * 输入流永久丢失一个 token，后续声明解析错乱（如跨行
	 * `int\nmain(void)` 报 "expected declaration or function
	 * definition"）。 */
	struct token after;
	bool after_valid = false;

	/* --- buffer the whole declaration --- */
	cur = tok;
	{
		for (;;) {
			if (tok.kind == TEOF)
				break;
			if (bn >= cap) {
				cap = cap ? cap * 2 : 64;
				buf = xreallocarray(buf, cap, sizeof *buf);
			}
			buf[bn++] = tok;
			if (bd == 0 && pd == 0 && dd == 0 && tok.kind == TSEMICOLON) {
				next();
				after = tok;
				after_valid = true;
				break;
			}
			if (tok.kind == TLBRACE)
				++bd;
			else if (tok.kind == TRBRACE) {
				--bd;
				if (bd == 0 && pd == 0 && dd == 0) {
					next();
					after = tok;
					after_valid = true;
					break;
				}
			} else if (tok.kind == TLPAREN)
				++pd;
			else if (tok.kind == TRPAREN)
				--pd;
			else if (tok.kind == TLBRACK)
				++dd;
			else if (tok.kind == TRBRACK)
				--dd;
			next();
		}
	}

	/* --- find the direct function declarator's parameter list: the first
	 * '(' at depth 0 that directly follows an identifier (the function
	 * name) and is not part of a qualified `Class::method` name --- */
	{
		int depth = 0;
		bool prev_id = false, prev_colon2 = false;
		for (i = 0; i < bn; i++) {
			enum tokenkind k2 = buf[i].kind;
			if (k2 == TLPAREN) {
				if (depth == 0 && prev_id && !prev_colon2) {
					pl = i + 1;
					break;
				}
				++depth;
			} else if (k2 == TRPAREN) {
				if (depth > 0)
					--depth;
			} else if (k2 == TLBRACE) {
				++depth;
			} else if (k2 == TRBRACE) {
				if (depth > 0)
					--depth;
			}
			prev_id = k2 >= TIDENT;
			prev_colon2 = k2 == TCOLONCOLON;
		}
	}
	if (pl == (size_t)-1)
		goto not_abbrev;
	/* find the matching ')' */
	{
		int depth = 0;
		for (i = pl; i < bn; i++) {
			if (buf[i].kind == TLPAREN)
				++depth;
			else if (buf[i].kind == TRPAREN) {
				/* depth == 0: this ')' closes the '(' that pl points
				 * past.  (A plain `--depth == 0` test would first
				 * decrement 0 to -1 and never match, so ple would
				 * drift into the function body and count `auto`
				 * locals as template parameters.) */
				if (depth == 0) {
					ple = i;
					break;
				}
				--depth;
			}
		}
	}
	if (ple == (size_t)-1)
		goto not_abbrev;

	/* --- count the `auto` placeholder parameters --- */
	for (i = pl; i < ple; i++)
		if (buf[i].kind == TAUTO)
			++nauto;
	if (!nauto)
		goto not_abbrev;
	if (nauto > 8)
		error_code(E_SYNTAX, &tok.loc,
		    "too many 'auto' parameters in abbreviated function template");

	/* --- rewrite into a template declaration --- */
	tpl = buf[0];
	wtoks = xmalloc((bn + nauto * 24 + 32) * sizeof *wtoks);
	cpp_tb(wtoks, &wn, tpl, 0, "template");
	cpp_tb(wtoks, &wn, tpl, TLESS, NULL);
	for (k = 0; k < nauto; k++) {
		char tn[32];
		if (k)
			cpp_tb(wtoks, &wn, tpl, TCOMMA, NULL);
		cpp_tb(wtoks, &wn, tpl, 0, "typename");
		snprintf(tn, sizeof tn, "__T%d", k);
		cpp_tb(wtoks, &wn, tpl, 0, tn);
	}
	cpp_tb(wtoks, &wn, tpl, TGREATER, NULL);
	/* requires-clause from the constrained parameters */
	{
		bool any = false;
		for (i = pl; i < ple; i++) {
			if (buf[i].kind == TAUTO && i > pl &&
			    buf[i - 1].kind >= TIDENT) {
				struct cpp_template *con;
				for (con = g_cpp_templates; con; con = con->next)
					if (con->is_concept &&
					    strcmp(con->name,
					    tokenstr(buf[i - 1].kind)) == 0) {
						any = true;
						break;
					}
				if (any)
					break;
			}
		}
		if (any) {
			int k2 = 0;
			cpp_tb(wtoks, &wn, tpl, 0, "requires");
			for (i = pl; i < ple; i++) {
				if (buf[i].kind == TAUTO) {
					struct cpp_template *con = NULL;
					if (i > pl && buf[i - 1].kind >= TIDENT) {
						for (con = g_cpp_templates; con; con = con->next)
							if (con->is_concept && strcmp(con->name,
							    tokenstr(buf[i - 1].kind)) == 0)
								break;
					}
					if (con) {
						char tn[32];
						if (k2 > 0)
							cpp_tb(wtoks, &wn, tpl, TLAND, NULL);
						cpp_tb(wtoks, &wn, tpl, 0, con->name);
						cpp_tb(wtoks, &wn, tpl, TLESS, NULL);
						snprintf(tn, sizeof tn, "__T%d", k2);
						cpp_tb(wtoks, &wn, tpl, 0, tn);
						cpp_tb(wtoks, &wn, tpl, TGREATER, NULL);
					}
					++k2;
				}
			}
		}
	}
	/* the rewritten declaration: drop each concept name that precedes a
	 * constrained `auto`, replace every `auto` with its `__Tk` */
	{
		int k2 = 0;
		for (i = 0; i < bn; i++) {
			if (i >= pl && i < ple && buf[i].kind >= TIDENT &&
			    i + 1 < ple && buf[i + 1].kind == TAUTO) {
				struct cpp_template *con;
				for (con = g_cpp_templates; con; con = con->next)
					if (con->is_concept && strcmp(con->name,
					    tokenstr(buf[i].kind)) == 0)
						break;
				if (con)
					continue; /* drop the concept name */
			}
			if (i >= pl && i < ple && buf[i].kind == TAUTO) {
				char tn[32];
				snprintf(tn, sizeof tn, "__T%d", k2++);
				cpp_tb(wtoks, &wn, tpl, 0, tn);
				continue;
			}
			wtoks[wn++] = buf[i];
		}
	}

	/* --- replay `template <...> [requires ...] decl` --- */
	{
		struct token guard = tok; /* token after the declaration */
		tokpush(&guard, 1);
		tokpush(wtoks, wn);
		next();
		cpp_template_decl(s, NULL);
	}
	return true;

not_abbrev:
	/* restore the stream: `tok` is already buf[0]; the remaining
	 * buffered tokens replay from the pushed context.  The buffer loop
	 * consumed one extra token (';' / '}' terminator's successor) with
	 * its final next(); push it back too, or the source stream loses it
	 * and the following declaration misparses. */
	tok = cur;
	if (after_valid)
		tokpush(&after, 1);      /* popped last */
	if (bn > 0)
		tokpush(buf + 1, bn - 1); /* popped first */
	return false;
}

/* Dummy function-pointer type + decl for the template-call placeholder
 * expression (satisfies the TLPAREN "called object" checks until the real
 * instantiation replaces it). */
static struct decl *
cpp_tmpl_dummy_callee(void)
{
	static struct type *fn;
	static struct decl *d;

	if (!d) {
		fn = mktype(TYPEFUNC, 0);
		fn->base = &typevoid;
		fn->u.func.isvararg = false;
		fn->u.func.params = NULL;
		fn->u.func.nparam = 0;
		d = mkdecl("__tmpl", DECLFUNC, fn, QUALNONE, LINKNONE);
		d->value = mkglobal(d);
	}
	return d;
}

/* primaryexpr helper: `name` is an undeclared identifier that names a
 * function template.  Record the pending template call and return a
 * placeholder callee; the TLPAREN lowering performs the instantiation
 * once the argument types are known. */
struct expr *
cpp_tmpl_placeholder(const char *name)
{
	struct expr *e;

	if (g_cpp_tmpl_depth >= 64)
		error_code(E_TEMPLATE, &tok.loc, "template call nesting too deep");
	g_cpp_tmpl_stack[g_cpp_tmpl_depth++] = name;
	e = mkexpr(EXPRIDENT, mkpointertype(cpp_tmpl_dummy_callee()->type, QUALNONE),
	           NULL);
	e->u.ident.decl = cpp_tmpl_dummy_callee();
	e->lvalue = false;
	return e;
}

/* Parse explicit template arguments after a function-template name:
 * `f<int, 42, N>(...)`.  Each argument is either a type (parsed with
 * typename()) or a non-type constant expression (folded to a value).
 * The leading explicit arguments fill the template parameters before any
 * call-site deduction.  On return `tok` is positioned just past the
 * closing '>'. */
static struct expr *cpp_tmpl_const_arg(struct scope *s);

void
cpp_tmpl_explicit_parse(struct scope *s)
{
	extern struct type *typename(struct scope *, enum typequal *,
	    struct expr **);
	extern struct cpp_template *cpp_tmpl_find(const char *);
	enum typequal tq;
	struct expr *toeval;
	struct cpp_template *tmpl;
	struct cpp_tmpl_param *p = NULL;

	g_cpp_tmpl_expl_n = 0;
	expect(TLESS, "after template name");
	/* Consult the template's parameter kinds so each explicit argument is
	 * parsed directly as a type or a constant value (mirrors the class
	 * template instantiation).  A type-dependent NTTP (`template<typename
	 * T, T N>`) falls out naturally: N is a value of the *argument's*
	 * concrete type, recorded here and bound at instantiation. */
	if (g_cpp_tmpl_depth > 0)
		tmpl = cpp_tmpl_find(g_cpp_tmpl_stack[g_cpp_tmpl_depth - 1]);
	else
		tmpl = NULL;
	if (tmpl)
		p = tmpl->params;
	while (tok.kind != TGREATER) {
		if (g_cpp_tmpl_expl_n >= 16)
			error_code(E_SYNTAX, &tok.loc, "too many template arguments");
		if (p && p->is_nttp) {
			/* non-type parameter: a constant integer expression */
			struct expr *ev = cpp_tmpl_const_arg(s);
			g_cpp_tmpl_expl_types[g_cpp_tmpl_expl_n] = ev->type;
			g_cpp_tmpl_expl_vals[g_cpp_tmpl_expl_n] =
			    ev->u.constant.u;
			g_cpp_tmpl_expl_isval[g_cpp_tmpl_expl_n] = true;
		} else {
			tq = QUALNONE;
			toeval = NULL;
			struct type *tt = typename(s, &tq, &toeval);
			if (!tt || toeval)
				error_code(E_TEMPLATE, &tok.loc,
				    "template argument is not a type");
			g_cpp_tmpl_expl_types[g_cpp_tmpl_expl_n] = tt;
			g_cpp_tmpl_expl_isval[g_cpp_tmpl_expl_n] = false;
		}
		++g_cpp_tmpl_expl_n;
		if (p)
			p = p->next;
		if (tok.kind == TGREATER)
			break;
		expect(TCOMMA, "',' or '>' in template argument list");
	}
	next(); /* consume '>' */
}

static struct expr *
cpp_tmpl_const_arg(struct scope *s)
{
	extern struct expr *condexpr(struct scope *);
	extern struct expr *eval(struct expr *);
	extern void tokpush(struct token *, size_t);
	struct token buf[256];
	size_t bn = 0;
	int depth = 0;
	struct token sep;

	for (;;) {
		if (bn >= 256)
			error_code(E_TEMPLATE, &tok.loc, "template argument too long");
		if (tok.kind == TLPAREN || tok.kind == TLBRACK)
			++depth;
		else if (tok.kind == TRPAREN || tok.kind == TRBRACK) {
			if (depth)
				--depth;
		} else if ((tok.kind == TGREATER || tok.kind == TCOMMA) &&
		    depth == 0) {
			sep = tok;
			break;
		}
		buf[bn++] = tok;
		next();
	}
	if (!bn)
		error_code(E_SYNTAX, &tok.loc, "expected template argument");
	{
		/* replay the buffered expression in front of a guard so the
		 * parser stops at the end of it instead of consuming the source
		 * '>' (a binary operator) that follows */
		size_t d = tokctx_depth();
		struct token guard = {0};
		guard.kind = TSEMICOLON;
		tokpush(&guard, 1);
		tokpush(buf, bn);
		next(); /* position tok at the first buffered token */
		struct expr *ev = eval(condexpr(s));
		if (!ev || ev->kind != EXPRCONST || !(ev->type->prop & PROPINT))
			error_code(E_TEMPLATE, &tok.loc,
			    "non-type template argument must be a constant integer expression");
		/* restore the stream to the source '>' (never consumed) */
		tokctx_rewind(d);
		tok = sep;
		return ev;
	}
}

/* Append one token to a growing token buffer (used to accumulate the
 * per-parameter constraints of `template<Concept T>` declarations). */
static void
cpp_constraint_add(struct token **buf, size_t *n, size_t *cap, struct token t)
{
	if (*n >= *cap) {
		*cap = *cap ? *cap * 2 : 16;
		*buf = xreallocarray(*buf, *cap, sizeof **buf);
	}
	(*buf)[(*n)++] = t;
}

/* C++11 template type alias: `template<...> using Name = Type;` (e.g.
 * `template<typename T> using Vec = T;`).  The alias body is a type
 * expression over the template parameters; each use `Name<Args...>`
 * substitutes the argument types and resolves to the indicated type.
 * Registered at declaration time; instantiated on first use (see
 * cpp_tmpl_alias_instantiate). */
struct cpp_tmpl_alias {
	const char *name;
	int nparams;
	const char **param_names;
	struct token *toks;     /* alias type expression tokens up to ';' */
	size_t ntoks;
	struct cpp_tmpl_alias *next;
};
static struct cpp_tmpl_alias *g_cpp_aliases;

/* Register a template type alias `template<...> using Name = <body>;`.
 * `params`/`nparams` are the template parameter names; `toks/ntoks` is
 * the buffered alias-type expression (everything after `=` up to `;`). */
void
cpp_register_alias(const char *name, const char **params, int nparams,
                   struct token *toks, size_t ntoks)
{
	struct cpp_tmpl_alias *a = xmalloc(sizeof *a);
	int i;
	a->name = xmalloc(strlen(name) + 1);
	strcpy((char *)a->name, name);
	a->nparams = nparams;
	a->param_names = xmalloc((size_t)nparams * sizeof *a->param_names);
	for (i = 0; i < nparams; i++) {
		a->param_names[i] = params[i] ? xmalloc(strlen(params[i]) + 1)
		    : NULL;
		if (a->param_names[i])
			strcpy((char *)a->param_names[i], params[i]);
	}
	a->toks = xmalloc(ntoks * sizeof *a->toks);
	a->ntoks = ntoks;
	memcpy(a->toks, toks, ntoks * sizeof *a->toks);
	a->next = g_cpp_aliases;
	g_cpp_aliases = a;
}

/* Look up a template type alias by name (or NULL). */
bool
cpp_tmpl_alias_lookup(const char *name)
{
	struct cpp_tmpl_alias *a;
	for (a = g_cpp_aliases; a; a = a->next)
		if (strcmp(a->name, name) == 0)
			return true;
	return false;
}

/* Find the registered alias with the given name. */
static struct cpp_tmpl_alias *
cpp_find_alias(const char *name)
{
	struct cpp_tmpl_alias *a;
	for (a = g_cpp_aliases; a; a = a->next)
		if (strcmp(a->name, name) == 0)
			return a;
	return NULL;
}

/* Instantiate a template type alias `Name<Args...>` and return the
 * resolved type.  Parses the explicit `<...>` argument types (each as a
 * `typename()`), binds each template parameter to its argument type in a
 * temporary scope, then re-parses the buffered alias-body type in that
 * scope.  Returns NULL when it is not this kind of alias use. */
struct type *
cpp_tmpl_alias_instantiate(struct scope *s, const char *name)
{
	extern struct type *typename(struct scope *, enum typequal *,
	    struct expr **);
	extern struct scope *mkscope(struct scope *);
	extern void scopeputdecl(struct scope *, struct decl *);
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);
	extern void tokpush(struct token *, size_t);
	struct cpp_tmpl_alias *a = cpp_find_alias(name);
	struct type *args[16];
	struct scope *bs;
	struct type *result = NULL;
	int i, n = 0;

	if (!a)
		return NULL;
	expect(TLESS, "after template alias name");
	if (a->nparams > 16)
		error_code(E_SYNTAX, &tok.loc, "template alias '%s' has too many parameters", name);
	for (;;) {
		if (n >= a->nparams)
			error_code(E_SYNTAX, &tok.loc, "too many template arguments for alias '%s'", name);
		{
			enum typequal tq = QUALNONE;
			struct expr *toeval = NULL;
			args[n] = typename(s, &tq, &toeval);
			if (!args[n] || toeval)
				error_code(E_TEMPLATE, &tok.loc, "template alias argument is not a type");
		}
		++n;
		if (tok.kind == TGREATER)
			break;
		expect(TCOMMA, "',' or '>' in template alias argument list");
	}
	next(); /* consume '>' */
	if (n < a->nparams)
		error_code(E_TEMPLATE, &tok.loc, "too few template arguments for alias '%s'", name);

	/* Save the stream position after '>' so the alias-body replay can be
	 * rewound without losing the source token that follows `Name<...>`
	 * (the declarator), like the class-template instantiation path. */
	{
		struct token after_gt = tok;
		/* bind the template parameters as DECLTYPE names, then resolve
		 * the alias body in that context */
		bs = mkscope(s);
		for (i = 0; i < a->nparams; i++)
			scopeputdecl(bs, mkdecl((char *)a->param_names[i], DECLTYPE,
			    args[i], QUALNONE, LINKNONE));
		{
			size_t d = tokctx_depth();
			struct token guard = {0};
			guard.kind = TSEMICOLON;
			tokpush(&guard, 1);
			tokpush(a->toks, a->ntoks);
			next();
			{
				enum typequal tq = QUALNONE;
				struct expr *toeval = NULL;
				result = typename(bs, &tq, &toeval);
			}
			tokctx_rewind(d);
			tok = after_gt;
		}
	}
	return result;
}

/* Parse `template<...> using Name = Type;` and register the alias.
 * Called from cpp_template_decl after the parameter list is parsed and
 * the current token is the `using` keyword. */
static void
cpp_template_alias(struct cpp_template *tmpl)
{
	const char **pnames;
	const char *aname;
	struct token *atoks = NULL;
	size_t an = 0, acap = 0, i;
	struct cpp_tmpl_param *p;
	extern void next(void);

	next(); /* consume 'using' */
	if (tok.kind < TIDENT)
		error_code(E_SYNTAX, &tok.loc, "expected alias name after 'using'");
	aname = tokenstr(tok.kind);
	next(); /* consume alias name */
	expect(TASSIGN, "after alias name in using alias declaration");

	/* collect template parameter names from tmpl */
	pnames = xmalloc((size_t)tmpl->nparams * sizeof *pnames);
	i = 0;
	for (p = tmpl->params; p; p = p->next, i++)
		pnames[i] = p->name;

	/* buffer the alias type expression up to ';' */
	for (;;) {
		if (tok.kind == TSEMICOLON)
			break;
		if (tok.kind == TEOF)
			error_code(E_SYNTAX, &tok.loc, "unterminated template alias declaration");
		if (an >= acap) {
			acap = acap ? acap * 2 : 32;
			atoks = xreallocarray(atoks, acap, sizeof *atoks);
		}
		atoks[an++] = tok;
		next();
	}
	next(); /* consume ';' */
	cpp_register_alias(aname, pnames, tmpl->nparams, atoks, an);
}

/* Parse `template < typename T, class U, ... >` and buffer the following
 * declaration.  Nothing is defined yet; instantiation happens on first
 * use with concrete type arguments.  `owner` is the enclosing class for
 * a member template (`template<...> T get() {...}` inside a class body),
 * or NULL for a file-scope function/class template. */
static void
cpp_template_decl(struct scope *s, struct type *owner)
{
	struct cpp_template *tmpl;
	struct cpp_tmpl_param *p, **pe;
	struct token *toks;
	size_t ntoks = 0, cap = 0;
	int bd = 0;
	bool param;
	/* constraints accumulated from `template<Concept T>` type parameters;
	 * merged with an explicit requires-clause (if any) below */
	struct token *pctoks = NULL;
	size_t pcn = 0, pccap = 0;


	next(); /* consume 'template' */
	expect(TLESS, "after 'template'");
	tmpl = xmalloc(sizeof(*tmpl));
	tmpl->name = NULL;
	tmpl->nparams = 0;
	tmpl->params = NULL;
	tmpl->toks = NULL;
	tmpl->ntoks = 0;
	tmpl->is_class = false;
	tmpl->is_member = owner != NULL;
	tmpl->owner = owner;
	tmpl->insts = NULL;
	tmpl->insts_end = &tmpl->insts;
	tmpl->cls_insts = NULL;
	tmpl->cls_insts_end = &tmpl->cls_insts;
	tmpl->constraint = NULL;
	tmpl->nconstraint = 0;
	tmpl->next = NULL;
	pe = &tmpl->params;

	/* template parameter list: `typename T` / `class T` (type parameter),
	 * `int N` / `auto N` (non-type template parameter, C++17/C++20),
	 * comma separated; a trailing parameter pack: `typename... Args` /
	 * `class... Args` */
	/* Parameter-list scope: type parameters (`T`) are visible as DECLTYPE
	 * decls so a later non-type parameter can name them (`template<typename T,
	 * T N>` — P0847-dependent-type NTTP). */
	{
		extern struct scope *mkscope(struct scope *);
		extern void scopeputdecl(struct scope *, struct decl *);
		struct scope *ps = mkscope(s);
		for (;;) {
			enum cpp_tokenkind k = cpp_tok_kind();
			if (k == CPP_TTYPENAME || k == CPP_TCLASS) {
				next(); /* consume typename/class */
				p = xmalloc(sizeof(*p));
				p->is_pack = false;
				p->is_dep_nttp = false;
				p->is_nttp = false;
				p->nttp_type = NULL;
				if (tok.kind == TELLIPSIS) {
					p->is_pack = true;
					next();
				}
				if (tok.kind < TIDENT)
					error_code(E_SYNTAX, &tok.loc, "expected template parameter name");
				p->name = xmalloc(strlen(tokenstr(tok.kind)) + 1);
				strcpy((char *)p->name, tokenstr(tok.kind));
				p->next = NULL;
				*pe = p;
				pe = &p->next;
				++tmpl->nparams;
				/* a non-pack type parameter is nameable as the type of a
				 * later NTTP; put it in the parameter-list scope */
				if (!p->is_pack) {
					extern struct decl *mkdecl(char *, enum declkind,
					    struct type *, enum typequal, enum linkage);
					scopeputdecl(ps, mkdecl((char *)p->name,
					    DECLTYPE, &typevoid, QUALNONE, LINKNONE));
				}
				next();
			} else if (tok.kind == TAUTO) {
				/* `template<auto N>`: deduced non-type parameter (the C
				 * lexer emits the TAUTO storage-class token for `auto`) */
				next(); /* consume auto */
				p = xmalloc(sizeof(*p));
				p->is_pack = false;
				p->is_dep_nttp = false;
				p->is_nttp = true;
				p->nttp_type = NULL;
				if (tok.kind < TIDENT)
					error_code(E_SYNTAX, &tok.loc, "expected template parameter name");
				p->name = xmalloc(strlen(tokenstr(tok.kind)) + 1);
				strcpy((char *)p->name, tokenstr(tok.kind));
				p->next = NULL;
				*pe = p;
				pe = &p->next;
				++tmpl->nparams;
				next();
			} else if (tok.kind >= TIDENT) {
			/* C++20 constrained type parameter: `template<Concept T>`
			 * (also `template<Concept<Args...> T>` with explicit concept
			 * arguments, and a pack `template<Concept... T>`).  The
			 * constraint `Concept<T>` (or `Concept<Args..., T>`) is
			 * recorded and merged with any requires-clause below;
			 * satisfaction is checked at instantiation by the same
			 * concept machinery as the requires-clause. */
			const char *cnm = tokenstr(tok.kind);
			struct cpp_template *con;
			for (con = g_cpp_templates; con; con = con->next)
				if (con->is_concept && strcmp(con->name, cnm) == 0)
					break;
			if (con) {
				struct token cname = tok;
				bool has_args = false;
				int tdepth = 0;
				next(); /* consume the concept name */
				/* constraint so far: the concept name */
				cpp_constraint_add(&pctoks, &pcn, &pccap, cname);
				/* explicit concept arguments: `template<C<int> T>` */
				if (tok.kind == TLESS) {
					has_args = true;
					for (;;) {
						cpp_constraint_add(&pctoks, &pcn, &pccap, tok);
						if (tok.kind == TLESS)
							++tdepth;
						else if (tok.kind == TGREATER) {
							--tdepth;
							if (tdepth == 0) {
								next();
								break;
							}
						}
						next();
					}
				}
				p = xmalloc(sizeof(*p));
				p->is_pack = false;
				p->is_dep_nttp = false;
				p->is_nttp = false;
				p->nttp_type = NULL;
				if (tok.kind == TELLIPSIS) {
					p->is_pack = true;
					next();
				}
				if (tok.kind < TIDENT)
					error_code(E_SYNTAX, &tok.loc, "expected template parameter name");
				p->name = xmalloc(strlen(tokenstr(tok.kind)) + 1);
				strcpy((char *)p->name, tokenstr(tok.kind));
				p->next = NULL;
				*pe = p;
				pe = &p->next;
				++tmpl->nparams;
				if (!p->is_pack) {
					extern struct decl *mkdecl(char *, enum declkind,
					    struct type *, enum typequal, enum linkage);
					scopeputdecl(ps, mkdecl((char *)p->name,
					    DECLTYPE, &typevoid, QUALNONE, LINKNONE));
				}
				/* complete the constraint: `Concept < [args...,] T >` */
				if (!has_args) {
					struct token t = cname;
					t.kind = TLESS;
					cpp_constraint_add(&pctoks, &pcn, &pccap, t);
				} else {
					/* retract the '>' that closed the explicit args
					 * and insert the parameter as the last argument */
					--pcn;
					struct token t = cname;
					t.kind = TCOMMA;
					cpp_constraint_add(&pctoks, &pcn, &pccap, t);
				}
				{
					struct token t = cname;
					t.kind = tokenget(p->name, strlen(p->name));
					cpp_constraint_add(&pctoks, &pcn, &pccap, t);
					t.kind = TGREATER;
					cpp_constraint_add(&pctoks, &pcn, &pccap, t);
				}
				next(); /* consume the parameter name */
				goto param_done;
			}
			/* not a concept: an identifier here is a *type name* used as
			 * a non-type parameter's type — `template<typename T, T N>`
			 * (dependent NTTP) or a plain fixed-type NTTP.  Fall through
			 * to the common non-type-parameter handling below. */
			goto nttp_common;
			} else {
			/* non-type template parameter with a fixed type: `int N`,
			 * or a type-dependent one: `T N` where T is an earlier
			 * type parameter (nameable in the parameter-list scope). */
			extern struct decl *parameter(struct scope *);
nttp_common:
			struct decl *pd = NULL;
			if (tok.kind >= TIDENT) {
				pd = scopegetdecl(ps, tokenstr(tok.kind), 1);
				if (pd && pd->kind == DECLTYPE) {
					/* `template<typename T, T N>`: dependent NTTP.
					 * nttp_type stays NULL so instantiation binds
					 * the concrete type of the argument. */
					next(); /* consume the type-parameter name */
					if (tok.kind < TIDENT)
						error_code(E_TEMPLATE, &tok.loc,
						    "expected non-type template parameter name");
					p = xmalloc(sizeof(*p));
					p->is_pack = false;
					p->is_dep_nttp = true;
					p->is_nttp = true;
					p->nttp_type = NULL;
					p->name = xmalloc(strlen(tokenstr(tok.kind)) + 1);
					strcpy((char *)p->name, tokenstr(tok.kind));
					p->next = NULL;
					*pe = p;
					pe = &p->next;
					++tmpl->nparams;
					next();
					goto param_done;
				}
				pd = NULL;
			}
			pd = parameter(ps);
			if (!pd || !pd->type ||
			    !(pd->type->prop & (PROPINT | PROPREAL)))
				error_code(E_TEMPLATE, &tok.loc,
				    "non-type template parameter must have integer or enum type");
			p = xmalloc(sizeof(*p));
			p->is_pack = false;
			p->is_dep_nttp = false;
			p->is_nttp = true;
			p->nttp_type = pd->type;
			p->name = pd->name ?
			    strdup(pd->name) : strdup("__nttp");
			p->next = NULL;
			*pe = p;
			pe = &p->next;
			++tmpl->nparams;
		}
param_done:
		/* C++11 default template argument: `template<typename T = int>` /
		 * `template<int N = 5>`.  Buffer the tokens after `=` up to the
		 * next top-level ',' or '>' into the parameter's default span.
		 * Applying the default (for an omitted argument) happens at
		 * instantiation in cpp_tmpl_deduce. */
		p->deftoks = NULL;
		p->ndeftoks = 0;
		if (tok.kind == TASSIGN) {
			struct token *dtoks = NULL;
			size_t dn = 0, dcap = 0, ddep = 0;
			next(); /* consume '=' */
			for (;;) {
				if (tok.kind == TEOF)
					error_code(E_TEMPLATE, &tok.loc,
					    "unterminated default template argument");
				if (ddep == 0 && (tok.kind == TCOMMA || tok.kind == TGREATER))
					break;
				if (tok.kind == TLESS || tok.kind == TLPAREN ||
				    tok.kind == TLBRACK)
					++ddep;
				else if (tok.kind == TGREATER || tok.kind == TRPAREN ||
				    tok.kind == TRBRACK) {
					if (ddep > 0)
						--ddep;
				}
				if (dn >= dcap) {
					dcap = dcap ? dcap * 2 : 16;
					dtoks = xreallocarray(dtoks, dcap, sizeof *dtoks);
				}
				dtoks[dn++] = tok;
				next();
			}
			p->deftoks = dtoks;
			p->ndeftoks = dn;
		}
		if (tok.kind == TGREATER)
			break;
		if (p->is_pack)
			error_code(E_TEMPLATE, &tok.loc, "template parameter pack must be the last parameter");
		expect(TCOMMA, "',' or '>' in template parameter list");
	}
	}
	next(); /* consume '>' */

	/* C++11 template type alias: `template<...> using Name = Type;`.
	 * Registered as a type alias (not a function/class template). */
	if (cpp_tok_kind() == CPP_TUSING) {
		cpp_template_alias(tmpl);
		return;
	}

	/* C++20 concept definition: `template<...> concept Name = expr;`.
	 * The concept body (a constant boolean expression over the template
	 * parameters) is buffered; a use `requires Integral<T>` looks the
	 * concept up and substitutes the argument types. */
	if (cpp_tok_kind() == CPP_TCONCEPT) {
		struct cpp_template *ct = xmalloc(sizeof *ct);
		struct token *ctoks = NULL;
		size_t cn = 0, ccap = 0;
		*ct = *tmpl; /* copy params etc. */
		ct->is_concept = true;
		ct->toks = NULL;
		ct->ntoks = 0;
		ct->constraint = NULL;
		ct->nconstraint = 0;
		ct->insts = NULL;
		ct->insts_end = &ct->insts;
		ct->cls_insts = NULL;
		ct->cls_insts_end = &ct->cls_insts;
		ct->next = NULL;
		free(tmpl); /* the original was just a scaffolding copy */
		tmpl = ct;
		next(); /* consume 'concept' */
		if (tok.kind < TIDENT)
			error_code(E_SYNTAX, &tok.loc, "expected concept name after 'concept'");
		tmpl->name = xmalloc(strlen(tokenstr(tok.kind)) + 1);
		strcpy((char *)tmpl->name, tokenstr(tok.kind));
		next(); /* consume the concept name */
		expect(TASSIGN, "after concept name");
		/* buffer `expr;` — everything up to the ';' at brace depth 0.
		 * A requires-expression body (`requires (T a) { a+a; }`) brings
		 * its own braces and semicolons, so those must not terminate the
		 * concept definition. */
		{
			int cdepth = 0;
			for (;;) {
				if (tok.kind == TEOF)
					error_code(E_CTYPE, &tok.loc,
					    "unterminated concept definition '%s'",
					    tmpl->name);
				if (cdepth == 0 && tok.kind == TSEMICOLON)
					break;
				if (cdepth == 0 &&
				    cpp_tok_kind() == CPP_TREQUIRES) {
					/* a whole requires-expression: consume it
					 * (including its body braces) as one unit */
					struct token *rtoks = NULL;
					size_t rn = cpp_requires_span_len(&rtoks);
					for (size_t k = 0; k < rn; k++) {
						if (cn >= ccap) {
							ccap = ccap ? ccap * 2 : 64;
							ctoks = xreallocarray(ctoks,
							    ccap, sizeof *ctoks);
						}
						ctoks[cn++] = rtoks[k];
					}
					free(rtoks);
					continue;
				}
				if (tok.kind == TLBRACE)
					++cdepth;
				else if (tok.kind == TRBRACE && cdepth > 0)
					--cdepth;
				if (cn >= ccap) {
					ccap = ccap ? ccap * 2 : 64;
					ctoks = xreallocarray(ctoks, ccap,
					    sizeof *ctoks);
				}
				ctoks[cn++] = tok;
				next();
			}
		}
		next(); /* consume ';' */
		tmpl->toks = ctoks;
		tmpl->ntoks = cn;
		*g_cpp_templates_end = tmpl;
		g_cpp_templates_end = &tmpl->next;
		return;
	}

	/* C++20 requires-clause: `template<...> requires Expr<T> decl`.
	 * Buffer the constraint expression tokens (everything from `requires`
	 * up to the start of the declaration).  The constraint is a boolean
	 * expression over concept uses (`Small<T>`, `Small<T> && NotVoid<T>`,
	 * `!Small<T>`); we consume tokens until the return type / function
	 * name of the declaration begins, or until '{' / ';'.  A constraint
	 * combinator (`&&` / `||` / `!`) keeps the following concept name in
	 * the clause. */
	if (cpp_tok_kind() == CPP_TREQUIRES) {
		struct token *ctoks = NULL;
		size_t cn = 0, ccap = 0;
		int depth = 0;
		bool after_op = false; /* previous token was && / || / ! */
		next(); /* consume 'requires' */
		for (;;) {
			if (tok.kind == TEOF)
				break;
			if (depth == 0 && (tok.kind == TLBRACE || tok.kind == TSEMICOLON))
				break;
			/* The declaration begins with the return type (a keyword
			 * like `int` or a type/function name), or '{' / ';'.  A
			 * constraint combinator (`&&` / `||` / `!`) means the
			 * next identifier is another concept name and stays part
			 * of the requires-clause; identifiers and template-arg
			 * brackets are also constraint tokens. */
			if (depth == 0 && cn > 0 && !after_op &&
			    (tok.kind >= TIDENT ||
			     (tok.kind != TLESS && tok.kind != TGREATER &&
			      tok.kind != TCOMMA && tok.kind != TLAND &&
			      tok.kind != TLOR && tok.kind != TLNOT)))
				break;
			if (tok.kind == TLESS || tok.kind == TLPAREN || tok.kind == TLBRACK)
				++depth;
			else if (tok.kind == TGREATER || tok.kind == TRPAREN ||
			    tok.kind == TRBRACK) {
				if (depth > 0)
					--depth;
			}
			if (cn >= ccap) {
				ccap = ccap ? ccap * 2 : 16;
				ctoks = xreallocarray(ctoks, ccap, sizeof *ctoks);
			}
			ctoks[cn++] = tok;
			after_op = tok.kind == TLAND || tok.kind == TLOR ||
			    tok.kind == TLNOT;
			next();
		}
		tmpl->constraint = ctoks;
		tmpl->nconstraint = cn;
		if (pcn) {
			/* merge the `template<Concept T>` parameter constraints
			 * with the requires-clause: `Concept<T> && Expr<T>` */
			struct token t = tok;
			struct token *all = xmalloc((pcn + 1 + cn) * sizeof *all);
			size_t an = 0, k;
			for (k = 0; k < pcn; k++)
				all[an++] = pctoks[k];
			t.kind = TLAND;
			all[an++] = t;
			for (k = 0; k < cn; k++)
				all[an++] = ctoks[k];
			tmpl->constraint = all;
			tmpl->nconstraint = an;
			free(ctoks);
			free(pctoks);
		}
	} else if (pcn) {
		/* no requires-clause: the parameter constraints are the whole
		 * constraint */
		tmpl->constraint = pctoks;
		tmpl->nconstraint = pcn;
	}

	/* class template: `template<...> class Foo { ... }` (struct/union too).
	 * Checked *after* the requires-clause: with `template<...> requires C<T>
	 * class Foo { ... };` the token following '>' is `requires`, not the
	 * class-key, so probing before the clause is consumed would leave
	 * is_class false and the trailing ';' of `};` unconsumed. */
	{
		enum cpp_tokenkind k = cpp_tok_kind();
		if (k == CPP_TCLASS || k == CPP_TSTRUCT || k == CPP_TUNION)
			tmpl->is_class = true;
	}

	/* buffer the rest of the declaration (return type .. body / ';') */
	toks = NULL;
	for (;;) {
		/* declaration-only template: stop at the ';' */
		if (bd == 0 && tok.kind == TSEMICOLON) {
			next();
			break;
		}
		if (ntoks >= cap) {
			cap = cap ? cap * 2 : 128;
			toks = xreallocarray(toks, cap, sizeof *toks);
		}
		toks[ntoks++] = tok;
		if (tok.kind == TLBRACE)
			++bd;
		else if (tok.kind == TRBRACE)
			--bd;
		next();
		/* end of a function body: the closing brace brought bd back to 0 */
		if (bd == 0 && toks[ntoks - 1].kind == TRBRACE)
			break;
		if (tok.kind == TEOF)
			break;
	}
	/* a class template's `};` leaves the trailing ';' here; consume it */
	if (tmpl->is_class && tok.kind == TSEMICOLON)
		next();
	tmpl->toks = toks;
	tmpl->ntoks = ntoks;

	/* the function name: first plain identifier (not a C++ keyword, not a
	 * template parameter name) in the buffered declaration.  Identifier
	 * tokens carry their spelling in the interned token table (tokenstr
	 * of the kind); `lit` points at the scanner's reused buffer and is
	 * not stable for identifiers. */
	for (size_t i = 0; i < ntoks; ++i) {
		const char *nm;
		if (toks[i].kind < TIDENT)
			continue;
		nm = tokenstr(toks[i].kind);
		/* a template operator overload: `template<typename T> auto
		 * operator()(...)`.  The method name is `operator_<op>` ("cl"
		 * for operator(), "pl" for operator+), matching the non-template
		 * lowering so the call site resolves it. */
		if (cpp_classify_ident(nm, strlen(nm)) == CPP_TOPERATOR) {
			const char *onm = NULL;
			if (i + 1 < ntoks)
				onm = cpp_op_mangle(toks[i + 1].kind);
			if (onm) {
				char *m = xmalloc(strlen("operator_") + strlen(onm) + 1);
				sprintf(m, "operator_%s", onm);
				tmpl->name = m;
				break;
			}
			continue;
		}
		if (cpp_classify_ident(nm, strlen(nm)) != CPP_TNONE)
			continue; /* keyword */
		param = false;
		for (p = tmpl->params; p; p = p->next)
			if (strcmp(p->name, nm) == 0) {
				param = true;
				break;
			}
		if (!param) {
			tmpl->name = xmalloc(strlen(nm) + 1);
			strcpy((char *)tmpl->name, nm);
			break;
		}
	}
	if (!tmpl->name)
		error_code(E_DECL, &tok.loc, "unable to determine template function name");

	*g_cpp_templates_end = tmpl;
	g_cpp_templates_end = &tmpl->next;
}

/* Is template parameter `i` a non-type parameter? */
static bool
tmpl_param_is_nttp(struct cpp_template *tmpl, int i)
{
	struct cpp_tmpl_param *p;
	for (p = tmpl->params; p && i > 0; p = p->next, --i)
		;
	return p && p->is_nttp;
}

/* Deduce template arguments from the call-site argument list: parameter i
 * takes the type of argument i (positional; sufficient for the common
 * `T f(T a, T b)` / `T f(T a, U b)` forms).  A trailing parameter pack
 * collects every remaining argument type. */
static bool
cpp_tmpl_deduce(struct cpp_template *tmpl, struct scope *s,
                struct expr *arglist,
                struct type **out, int *nout, unsigned long long *nttp_vals)
{
	struct cpp_tmpl_param *p;
	struct expr *a;
	int nfix = 0;
	int i = 0;

	for (p = tmpl->params; p && !p->is_pack; p = p->next)
		++nfix;
	/* explicit template arguments fill the leading parameters first */
	for (p = tmpl->params; i < g_cpp_tmpl_expl_n && p; ++i, p = p->next) {
		if (g_cpp_tmpl_expl_isval[i]) {
			out[i] = g_cpp_tmpl_expl_types[i];
			if (nttp_vals)
				nttp_vals[i] = g_cpp_tmpl_expl_vals[i];
		} else {
			out[i] = g_cpp_tmpl_expl_types[i];
		}
	}
	for (a = arglist; a; a = a->next, ++i) {
		if (i < 16) {
			if (p && p->is_nttp) {
				/* non-type template argument must be a constant
				 * integer expression; fold and record its value */
				extern struct expr *eval(struct expr *);
				struct expr *ev = eval(a);
				if (!ev || ev->kind != EXPRCONST ||
				    !(ev->type->prop & PROPINT))
					error_code(E_TEMPLATE, &tok.loc,
					    "non-type template argument must be a constant integer expression");
				out[i] = ev->type;
				if (nttp_vals)
					nttp_vals[i] = ev->u.constant.u;
			} else {
				out[i] = a->type;
			}
		} else {
			error_code(E_SYNTAX, &tok.loc, "too many arguments for template '%s'", tmpl->name);
		}
		if (p)
			p = p->next;
	}
	/* C++11 default template arguments: for remaining fixed parameters
	 * that carry a default (`template<typename T = int>`), evaluate the
	 * buffered default tokens to fill the missing argument. */
	if (i < nfix) {
		extern struct scope *mkscope(struct scope *);
		extern void scopeputdecl(struct scope *, struct decl *);
		extern struct decl *mkdecl(char *, enum declkind,
		    struct type *, enum typequal, enum linkage);
		extern struct type *typename(struct scope *, enum typequal *,
		    struct expr **);
		extern struct expr *condexpr(struct scope *);
		extern struct expr *eval(struct expr *);
		extern void tokpush(struct token *, size_t);
		struct scope *ds = mkscope(s);
		/* bind the already-resolved type parameters so a default may
		 * reference an earlier parameter (`template<typename T,
		 * typename U = T>`) */
		struct token deduce_tok = tok;
		int k;
		for (k = 0; k < i && k < nfix; k++) {
			struct cpp_tmpl_param *pp = tmpl->params;
			int j;
			for (j = 0; j < k && pp; j++, pp = pp->next)
				;
			if (!pp)
				break;
			if (pp->is_nttp)
				scopeputdecl(ds, mkdecl((char *)pp->name,
				    DECLCONST, out[k], QUALNONE, LINKNONE));
			else
				scopeputdecl(ds, mkdecl((char *)pp->name,
				    DECLTYPE, out[k], QUALNONE, LINKNONE));
		}
		for (; i < nfix && p; p = p->next, ++i) {
			if (!p->deftoks || p->ndeftoks == 0)
				return false; /* too few arguments, no default */
			size_t d = tokctx_depth();
			struct token guard = {0};
			guard.kind = TSEMICOLON;
			tokpush(&guard, 1);
			tokpush(p->deftoks, p->ndeftoks);
			next(); /* position tok at the first default token */
			if (p->is_nttp) {
				/* value default: `template<int N = 5>` */
				struct expr *ev = eval(condexpr(ds));
				tokctx_rewind(d);
				tok = deduce_tok;
				if (!ev || ev->kind != EXPRCONST ||
				    !(ev->type->prop & PROPINT))
					error_code(E_TEMPLATE, &p->deftoks[0].loc,
					    "default template argument is not a constant integer expression");
				out[i] = ev->type;
				if (nttp_vals)
					nttp_vals[i] = ev->u.constant.u;
			} else {
				/* type default: `template<typename T = int>` */
				enum typequal tq = QUALNONE;
				struct expr *toeval = NULL;
				struct type *tt = typename(ds, &tq, &toeval);
				tokctx_rewind(d);
				tok = deduce_tok;
				if (!tt || toeval)
					error_code(E_TEMPLATE, &p->deftoks[0].loc,
					    "default template argument is not a type");
				out[i] = tt;
			}
		}
	}
	if (i < nfix)
		return false; /* too few arguments for the fixed parameters */
	if (i > 16)
		error_code(E_SYNTAX, &tok.loc, "too many arguments for template '%s'", tmpl->name);
	*nout = i;
	return true;
}

/* C++20 constraint checking: evaluate the requires-clause of a template
 * against the deduced argument types.
 *
 * A constraint is a boolean expression over concept uses:
 *   `Small<T>`, `Small<T> && NotVoid<T>`, `!Small<T>`, `A<T> || B<T>`.
 * We look up each concept definition, replay its body tokens with the
 * template-parameter names bound to the actual types (already in scope as
 * DECLTYPE decls), parse the body as an expression and constant-fold it:
 * a nonzero result means that concept use is satisfied.  `&&`, `||`, `!`
 * combine sub-constraints recursively.  Returns true when there is no
 * constraint. */
static bool eval_constraint(struct token *c, size_t n, struct scope *bs);

/* Maximum constraint expansion depth (guards against recursive
 * concept definitions referencing each other).  This is a runaway guard,
 * not a conformance limit: legitimate deep template code (long concept
 * reference chains, `Concept<Concept<...>>` nesting) routinely exceeds a
 * couple of dozen levels, so the bound is set well above the constant
 * expression recursion limit (64) rather than at it. */
#define MAX_CONSTRAINT_DEPTH 256

/* Split the argument token span `args[0..nargs)` of a concept use
 * `Name < args... >` on top-level commas.  Returns a heap array of
 * 2 * (*nout) offsets: argument k spans args[o[2*k] .. o[2*k + 1]).
 * An argument may be several tokens long (`unsigned int`), so the
 * parameter -> argument mapping cannot be a plain index. */
static size_t *
concept_arg_spans(struct token *args, size_t nargs, size_t *nout)
{
	size_t *o = NULL, n = 0, cap = 0, k, start = 0;
	int d = 0;

	for (k = 0; k <= nargs; k++) {
		if (k == nargs || (d == 0 && args[k].kind == TCOMMA)) {
			if (n + 2 > cap) {
				cap = cap ? cap * 2 : 8;
				o = xreallocarray(o, cap, sizeof *o);
			}
			o[n++] = start;
			o[n++] = k;
			start = k + 1;
			continue;
		}
		if (args[k].kind == TLESS || args[k].kind == TLPAREN ||
		    args[k].kind == TLBRACK)
			++d;
		else if ((args[k].kind == TGREATER ||
		    args[k].kind == TRPAREN || args[k].kind == TRBRACK) &&
		    d > 0)
			--d;
	}
	*nout = n / 2;
	return o;
}

/* Expand a concept body: substitute the body's template parameters with
 * the argument tokens and recursively expand any concept uses inside the
 * body.  Returns a heap-allocated token array kept alive through the
 * tokpush/expr below. */
static struct token *
expand_concept_body(struct cpp_template *con, struct token *args,
    size_t nargs, struct scope *bs, size_t *outn, int depth)
{
	struct token *out = NULL, *sub = NULL;
	size_t on = 0, cap = 0, sn = 0, scap = 0;
	size_t i;
	size_t nspan;
	size_t *span = concept_arg_spans(args, nargs, &nspan);

	/* pass 1: substitute the concept's own parameter names by their
	 * argument spans, so the body is fully concrete w.r.t. this use.
	 * A later pass handles concept uses inside the body; because the
	 * substitution happened first, their argument tokens are already
	 * this use's actual types, not this body's parameter names. */
	for (i = 0; i < con->ntoks; i++) {
		struct token t = con->toks[i];
		if (t.kind >= TIDENT) {
			struct cpp_tmpl_param *pp;
			size_t k = 0;
			for (pp = con->params; pp; pp = pp->next, ++k)
				if (strcmp(pp->name, tokenstr(t.kind)) == 0)
					break;
			if (pp && k < nspan) {
				/* emit the whole argument span: the argument
				 * may be a multi-token type (`unsigned int`,
				 * `const char *`, …), not just one token */
				for (size_t q = span[2 * k]; q < span[2 * k + 1];
				    q++) {
					if (sn >= scap) {
						scap = scap ? scap * 2 : 32;
						sub = xreallocarray(sub, scap,
						    sizeof *sub);
					}
					sub[sn++] = args[q];
				}
				continue;
			}
		}
		if (sn >= scap) {
			scap = scap ? scap * 2 : 32;
			sub = xreallocarray(sub, scap, sizeof *sub);
		}
		sub[sn++] = t;
	}

	/* pass 2: expand concept uses in the substituted body stream. */
	for (i = 0; i < sn; i++) {
		if (sub[i].kind >= TIDENT && i + 2 < sn &&
		    sub[i + 1].kind == TLESS) {
			struct cpp_template *csub;
			for (csub = g_cpp_templates; csub; csub = csub->next)
				if (csub->is_concept &&
				    strcmp(csub->name, tokenstr(sub[i].kind)) ==
				    0)
					break;
			if (csub) {
				if (depth >= MAX_CONSTRAINT_DEPTH)
					error_code(E_TEMPLATE, &tok.loc,
					    "requires-clause evaluation too deep: "
					    "concept reference chain exceeds %d levels",
					    MAX_CONSTRAINT_DEPTH);
				size_t j = i + 2;
				int d = 1;
				while (j < sn && d > 0) {
					if (sub[j].kind == TLESS)
						++d;
					else if (sub[j].kind == TGREATER)
						--d;
					++j;
				}
				{
					struct token *body;
					size_t bn;
					body = expand_concept_body(csub,
					    &sub[i + 2], j - 1 - (i + 2),
					    bs, &bn, depth + 1);
					for (size_t q = 0; q < bn; q++) {
						if (on >= cap) {
							cap = cap ? cap * 2 : 32;
							out = xreallocarray(out, cap, sizeof *out);
						}
						out[on++] = body[q];
					}
				}
				i = j - 1;
				continue;
			}
		}
		if (on >= cap) {
			cap = cap ? cap * 2 : 32;
			out = xreallocarray(out, cap, sizeof *out);
		}
		out[on++] = sub[i];
	}
	free(sub);
	free(span);
	*outn = on;
	return out;
}

/* Expand a constraint token stream, replacing concept uses `Name<args>`
 * with their (recursively expanded) concept bodies. */
static struct token *
expand_constraint_tokens(struct token *c, size_t n, struct scope *bs,
    size_t *outn)
{
	struct token *out = NULL;
	size_t on = 0, cap = 0;
	size_t i;

	for (i = 0; i < n; ) {
		if (c[i].kind >= TIDENT && i + 2 < n &&
		    c[i + 1].kind == TLESS) {
			struct cpp_template *con;
			for (con = g_cpp_templates; con; con = con->next)
				if (con->is_concept &&
				    strcmp(con->name, tokenstr(c[i].kind)) == 0)
					break;
			if (con) {
				size_t j = i + 2;
				int d = 1;
				while (j < n && d > 0) {
					if (c[j].kind == TLESS)
						++d;
					else if (c[j].kind == TGREATER)
						--d;
					++j;
				}
				{
					struct token *body;
					size_t bn;
					body = expand_concept_body(con,
					    &c[i + 2], j - 1 - (i + 2),
					    bs, &bn, 0);
					for (size_t q = 0; q < bn; q++) {
						if (on >= cap) {
							cap = cap ? cap * 2 : 64;
							out = xreallocarray(out, cap, sizeof *out);
						}
						out[on++] = body[q];
					}
				}
				i = j;
				continue;
			}
		}
		if (on >= cap) {
			cap = cap ? cap * 2 : 64;
			out = xreallocarray(out, cap, sizeof *out);
		}
		out[on++] = c[i];
		++i;
	}
	*outn = on;
	return out;
}

/* Evaluate one concept use `Name<args>` by expanding it into plain
 * constant-expression tokens and folding the result. */
static bool
eval_concept_use(struct token *c, size_t n, struct scope *bs)
{
	struct token cur;
	struct expr *e;
	struct token *exp;
	size_t en;

	/* C++20 literal constraint: `requires true` / `requires false` are
	 * not concept uses — evaluate them directly (they arrive as a
	 * single TTRUE/TFALSE token, both keyword kinds < TIDENT). */
	if (n == 1 && c[0].kind == TTRUE)
		return true;
	if (n == 1 && c[0].kind == TFALSE)
		return false;
	if (n == 0 || c[0].kind < TIDENT)
		error_code(E_TEMPLATE, &tok.loc, "requires-clause must name a concept");
	exp = expand_constraint_tokens(c, n, bs, &en);
	if (!exp || !en)
		error_code(E_TEMPLATE, &tok.loc, "requires-clause must name a concept");

	{
		extern struct expr *expr(struct scope *);
		extern struct expr *eval(struct expr *);
		extern void tokpush(struct token *, size_t);
		size_t d = tokctx_depth();
		struct token guard = {0};
		cur = tok;
		/* a guard after the expanded expression so expr() stops at the
		 * end of it instead of falling through to the buffered token
		 * below (cur / the caller's context) */
		guard.kind = TSEMICOLON;
		tokpush(&guard, 1);
		tokpush(exp, en);
		next();
		e = expr(bs);
		/* discard any unconsumed expansion tokens, then resume at the
		 * token that was current before the evaluation */
		tokctx_rewind(d);
		tokpush(&cur, 1);
		next();
		e = eval(e);
		if (!e || !(e->type->prop & PROPINT) || e->kind != EXPRCONST)
			error_code(E_TEMPLATE, &tok.loc,
			    "requires-clause is not a constant boolean expression");
		return e->u.constant.u != 0;
	}
}

/* Recursively evaluate a constraint expression: `A && B`, `A || B`,
 * `!A`, or a single concept use `Name<args>`. */
static bool
eval_constraint(struct token *c, size_t n, struct scope *bs)
{
	size_t i;
	int depth = 0;

	if (!c || !n)
		return true;
	for (i = 0; i < n; i++) {
		if (c[i].kind == TLESS || c[i].kind == TLPAREN ||
		    c[i].kind == TLBRACK)
			++depth;
		else if ((c[i].kind == TGREATER || c[i].kind == TRPAREN ||
		    c[i].kind == TRBRACK) && depth > 0)
			--depth;
		else if (depth == 0) {
			if (c[i].kind == TLAND)
				return eval_constraint(c, i, bs) &&
				    eval_constraint(c + i + 1, n - i - 1, bs);
			if (c[i].kind == TLOR)
				return eval_constraint(c, i, bs) ||
				    eval_constraint(c + i + 1, n - i - 1, bs);
			if (c[i].kind == TLNOT)
				return !eval_constraint(c + i + 1, n - i - 1, bs);
		}
	}
	return eval_concept_use(c, n, bs);
}

static bool
cpp_check_constraint(struct cpp_template *tmpl, struct scope *bs)
{
	return eval_constraint(tmpl->constraint, tmpl->nconstraint, bs);
}

/* --- C++20 requires-expressions --------------------------------------
 *
 * `requires { reqs }` and `requires (params) { reqs }` are boolean
 * constant expressions that hold when every requirement is well-formed.
 * The expression is buffered as a token span (so a failed trial leaves
 * the source stream positioned after the whole expression), then each
 * requirement is checked in a trial parse: an ill-formed requirement
 * makes the whole expression false instead of a hard error.
 *
 * Requirement kinds:
 *   simple       `a + a;`                      (expression must parse)
 *   type         `typename T::value_type;`     (type must name a type)
 *   compound     `{ e } -> Concept<U>;`        (e parses; Concept<U> holds
 *                                               for decltype(e))
 *   nested       `requires Concept<T>;`        (constraint must hold)
 */

/* Copy the requires-expression token span starting at the current token
 * (`requires` keyword) through its closing '}'.  Nested braces / parens
 * (compound-requirement bodies, nested requires, lambda bodies in
 * requirements) are tracked so the outer '}' is found correctly.  Fills
 * *out with the tokens (the caller owns the allocation) and advances the
 * stream past the closing '}'; returns the number of tokens. */
static size_t
cpp_requires_span_len(struct token **out)
{
	int depth = 0, req_brace = 0;
	size_t n = 0, cap = 16;
	struct token *sp = xmalloc(cap * sizeof *sp);

	for (;;) {
		if (tok.kind == TEOF)
			break;
		if (n >= cap) {
			cap *= 2;
			sp = xreallocarray(sp, cap, sizeof *sp);
		}
		sp[n++] = tok;
		if (depth == 0 && tok.kind == TLBRACE) {
			if (req_brace) {
				++depth;
			} else {
				/* the body's opening brace */
				req_brace = 1;
				++depth;
			}
		} else if (depth > 0) {
			if (tok.kind == TLBRACE)
				++depth;
			else if (tok.kind == TRBRACE && --depth == 0)
				break;
		}
		next();
	}
	*out = sp;
	return n;
}

/* The token `t` is a `typename`/`class` (C++ tag) keyword? */
static bool
cpp_is_tag_kw(enum tokenkind k)
{
	return cpp_tok_kind() == CPP_TTYPENAME || cpp_tok_kind() == CPP_TCLASS;
}

/* Is `name` (an identifier) usable as a type in `s` — i.e. a typedef /
 * template parameter / class name?  Used by the type-requirement fallback
 * that checks `T::member` where T is a template parameter. */
static bool
cpp_ident_is_type(struct scope *s, const char *name)
{
	struct decl *d = scopegetdecl(s, name, 1);
	struct type *t;

	if (d && d->kind == DECLTYPE)
		return true;
	t = scopegettag(s, name, 1);
	if (t && (t->kind == TYPESTRUCT || t->kind == TYPEUNION))
		return true;
	/* a namespace is not a type */
	if (d && d->kind == DECLNAMESPACE)
		return false;
	return false;
}

/* Type requirement `typename X` / `typename T::value_type`: check that
 * the tokens name a valid type.  `typename()` handles the non-dependent
 * cases (`typename int`, `typename MyClass`); for `T::member` (T a
 * template parameter bound to a concrete class in `s`) we check that
 * member is a typedef of that class.  Returns true when satisfied. */
static bool
cpp_req_type_ok(struct scope *s, struct token *sp, size_t n)
{
	struct expr *toeval = NULL;
	enum typequal tq = QUALNONE;
	struct type *t = NULL;

	/* skip the `typename` keyword (not a C type specifier) */
	if (n > 0 && cpp_classify_token(sp[0]) == CPP_TTYPENAME) {
		sp++;
		n--;
	}
	if (n == 0)
		return false;

	/* `T :: member` with T a known class: the member is a nested
	 * typedef of the bound class.  (Higher levels — `A::B::member` or
	 * `T::U::member` — are not handled yet.)  m++ stores struct members
	 * (including in-class typedefs) in the aggregate member list, so look
	 * the name up there rather than in the class scope. */
	if (n >= 3 && sp[1].kind == TCOLONCOLON &&
	    cpp_ident_is_type(s, tokenstr(sp[0].kind))) {
		struct decl *td = scopegetdecl(s, tokenstr(sp[0].kind), 1);
		struct type *ct;
		if (td && td->kind == DECLTYPE)
			ct = td->type;
		else
			ct = scopegettag(s, tokenstr(sp[0].kind), 1);
		if (ct && (ct->kind == TYPESTRUCT || ct->kind == TYPEUNION)) {
			unsigned long long off = 0;
			return typemember(ct, tokenstr(sp[2].kind), &off) != NULL;
		}
		return false;
	}

	/* replay the span and parse it as a type-name; errors longjmp to the
	 * enclosing trial and report failure.  The parse must consume the
	 * whole span: `typename void::value_type` parses `void` and leaves
	 * `::value_type`, which must not count as naming a type. */
	{
		size_t d = tokctx_depth();
		struct token guard = {0};
		bool complete;
		guard.kind = TSEMICOLON;
		tokpush(&guard, 1);
		tokpush(sp, n);
		next();
		t = typename(s, &tq, &toeval);
		complete = tok.kind == TSEMICOLON;
		(void)tq; (void)toeval;
		tokctx_rewind(d);
		if (!complete)
			return false;
	}
	return t != NULL;
}

/* Parse `(param1, param2, ...)` into the requires scope `rs`.  The whole
 * parameter clause `sp[0..n)` (starting at '(') is replayed so each
 * parameter becomes a scope entry the requirement body can reference; an
 * ill-formed list makes the whole requires-expression false (the enclosing
 * trial catches the error).  Returns the span offset just past the ')'. */
static size_t
cpp_req_params(struct scope *rs, struct token *sp, size_t n)
{
	size_t i = 1;

	if (n == 0 || sp[0].kind != TLPAREN)
		return 0; /* no parameter clause */
	{
		size_t d = tokctx_depth();
		struct token guard = {0};
		guard.kind = TSEMICOLON;
		tokpush(&guard, 1);
		tokpush(sp, n);
		next(); /* tok = '(' */
		next(); /* skip '('; parameter() parses each parameter */
		while (tok.kind != TRPAREN) {
			struct decl *pd;
			pd = parameter(rs);
			/* parameter() returns the decl; register it in the requires
			 * scope so the requirement body can reference it */
			if (pd && pd->name)
				scopeputdecl(rs, pd);
			if (tok.kind == TRPAREN)
				break;
			expect(TCOMMA, "or ')' in requires parameter list");
		}
		next(); /* consume ')' */
		tokctx_rewind(d);
	}
	/* number of span tokens consumed: everything through the matching ')' */
	{
		int d = 0;
		for (; i < n; i++) {
			if (sp[i].kind == TLPAREN || sp[i].kind == TLESS ||
			    sp[i].kind == TLBRACK)
				++d;
			else if (sp[i].kind == TRPAREN || sp[i].kind == TGREATER ||
			    sp[i].kind == TRBRACK) {
				if (d > 0)
					--d;
				else if (sp[i].kind == TRPAREN)
					break;
			}
		}
	}
	return i < n ? i + 1 : n;
}

/* Trial-parse a single requirement span (simple expression).  Returns
 * true when the expression parses without an error. */
static bool
cpp_req_simple(struct scope *rs, struct token *sp, size_t n)
{
	struct expr *e;
	size_t d = tokctx_depth();
	struct token guard = {0};

	/* a guard after the requirement span: the outer requires-expression
	 * span is still buffered below in the token context, and an
	 * expression parser must not fall through to its leftover tokens */
	guard.kind = TSEMICOLON;
	tokpush(&guard, 1);
	tokpush(sp, n);
	next();
	e = expr(rs);
	tokctx_rewind(d);
	return e != NULL;
}

/* Compound requirement `{ e } -> Constraint` / `{ e }`: validate e and an
 * optional trailing constraint (a concept-id or a type) against decltype(e). */
static bool
cpp_req_compound(struct scope *rs, struct token *sp, size_t n)
{
	struct expr *e;
	size_t close = 0;
	int d = 0;
	size_t i;

	if (n == 0 || sp[0].kind != TLBRACE)
		return false;
	/* find the matching '}' of the braced expression */
	for (i = 1; i < n; i++) {
		if (sp[i].kind == TLBRACE)
			++d;
		else if (sp[i].kind == TRBRACE) {
			if (d == 0) {
				close = i;
				break;
			}
			--d;
		}
	}
	if (close == 0)
		return false;

	/* the braced expression must parse (inner expression, excluding the
	 * enclosing braces) */
	{
		size_t d2 = tokctx_depth();
		struct token guard = {0};
		guard.kind = TSEMICOLON;
		tokpush(&guard, 1);
		tokpush(sp + 1, close - 1 > 0 ? close - 1 : 0);
		next();
		e = expr(rs);
		tokctx_rewind(d2);
	}
	if (!e)
		return false;

	/* optional `noexcept` specifier, then optional `-> TypeConstraint` */
	{
		size_t j = close + 1;
		/* `noexcept` or `noexcept(...)` after the braced expression:
		 * a compound requirement may require the expression be
		 * non-throwing.  We only need to skip it syntactically here
		 * (the well-formedness check is the expression parse above). */
		if (j < n && sp[j].kind == CPP_TNOEXCEPT) {
			j++;
			if (j < n && sp[j].kind == TLPAREN) {
				int nd = 1;
				j++;
				while (j < n && nd > 0) {
					if (sp[j].kind == TLPAREN)
						++nd;
					else if (sp[j].kind == TRPAREN &&
					    --nd == 0) {
						j++;
						break;
					}
					j++;
				}
			}
		}
		/* `{ e }` or `{ e } noexcept` : satisfied if the expression
		 * parsed (already checked above); no return-type constraint. */
		if (j >= n || sp[j].kind != TARROW)
			return true;
		size_t cn = n - (j + 1);
		struct token *ct = &sp[j + 1];
		/* a concept-id constraint: `C<T>` / `C<T1, T2>` */
		if (cn >= 1 && ct[0].kind >= TIDENT) {
			struct cpp_template *con;
			for (con = g_cpp_templates; con; con = con->next)
				if (con->is_concept && con->nparams > 0 &&
				    strcmp(con->name, tokenstr(ct[0].kind)) == 0)
					break;
			if (con && cn >= 3 && ct[1].kind == TLESS) {
				/* evaluate `C<decltype(e)>` */
				char iname[64];
				struct token *ntok;
				size_t k;
				struct token tc;
				struct decl *td;
				int nd = 0;
				snprintf(iname, sizeof iname, "__req%d",
				    ++g_cpp_lambda_count);
				/* a placeholder DECLTYPE decl in the requires
				 * scope: the constraint's first argument becomes
				 * the expression type */
				td = mkdecl(iname, DECLTYPE, e->type,
				    QUALNONE, LINKNONE);
				scopeputdecl(rs, td);
				ntok = xmalloc(cn * sizeof *ntok);
				memcpy(ntok, ct, cn * sizeof *ntok);
				ntok[2].kind = tokenget(iname, strlen(iname));
				/* validate the span is a plain concept-id */
				for (k = 3; k < cn; k++) {
					if (ntok[k].kind == TLESS)
						++nd;
					else if (ntok[k].kind == TGREATER) {
						if (nd == 0)
							break;
						--nd;
					} else if (nd == 0 &&
					    (ntok[k].kind == TSEMICOLON ||
					     ntok[k].kind == TRBRACE))
						break;
				}
				if (k == cn) {
					bool r = eval_concept_use(ntok, cn, rs);
					free(ntok);
					return r;
				}				free(ntok);
				return false;
			} else if (con) {
				/* `-> Concept` shorthand: a one-place concept
				 * constrained by the expression type:
				 * `Concept<decltype(e)>`. */
				struct token ntok[4] = {0};
				char iname[64];
				struct decl *td;
				snprintf(iname, sizeof iname, "__req%d",
				    ++g_cpp_lambda_count);
				td = mkdecl(iname, DECLTYPE, e->type,
				    QUALNONE, LINKNONE);
				scopeputdecl(rs, td);
				ntok[0] = ct[0];
				ntok[1].kind = TLESS;
				ntok[1].loc = ct[0].loc;
				ntok[2].kind = tokenget(iname, strlen(iname));
				ntok[2].loc = ct[0].loc;
				ntok[3].kind = TGREATER;
				ntok[3].loc = ct[0].loc;
				return eval_concept_use(ntok, 4, rs);
			}
		}
		/* `-> decltype(expr)` : a return-type requirement whose
		 * type-constraint is a decltype-specifier.  The compound
		 * expression's type must match the decltype'd expression's
		 * type (`{ e } -> decltype(e2)` holds iff the two types are
		 * the same).  We evaluate `e2` in the requires scope (where the
		 * parameters are visible) and compare types. */
		if (cn >= 1 && cpp_classify_token(ct[0]) == CPP_TDECLTYPE) {
			size_t k = 1, close = 0;
			int d = 0;
			if (cn < 3 || ct[1].kind != TLPAREN)
				return false;
			for (k = 1; k < cn; k++) {
				if (ct[k].kind == TLPAREN)
					++d;
				else if (ct[k].kind == TRPAREN && --d == 0) {
					close = k;
					break;
				}
			}
			if (close == 0)
				return false;
			{
				extern struct expr *expr(struct scope *);
				size_t d2 = tokctx_depth();
				struct token guard = {0};
				struct expr *e2;
				guard.kind = TSEMICOLON;
				tokpush(&guard, 1);
				tokpush(ct + 2, close - 2);
				next();
				e2 = expr(rs);
				tokctx_rewind(d2);
				if (!e2 || !e2->type)
					return false;
				return typesame(e->type, e2->type);
			}
		}
		/* otherwise a type constraint: the type must be valid */
		return cpp_req_type_ok(rs, ct, cn);
	}
}

/* Split a requires body `{ r1; r2; ... }` into requirement spans.
 * `body` excludes the outer braces.  Each span ends at (and excludes) the
 * terminating ';'.  Returns the number of spans and fills *spans (caller
 * frees); *nspans holds the count. */
static size_t *
cpp_req_split(struct token *body, size_t nbody, size_t *nspans)
{
	size_t *spans = NULL, ns = 0, cap = 0, start = 0;
	int d = 0;
	size_t i;

	for (i = 0; i <= nbody; i++) {
		if (i == nbody || (d == 0 && body[i].kind == TSEMICOLON)) {
			size_t len = i - start;
			if (len > 0) {
				if (ns + 2 > cap) {
					cap = cap ? cap * 2 : 8;
					spans = xreallocarray(spans, cap, sizeof *spans);
				}
				spans[ns++] = start;
				spans[ns++] = len;
			}
			start = i + 1;
			continue;
		}
		if (body[i].kind == TLBRACE)
			++d;
		else if (body[i].kind == TRBRACE && d > 0)
			--d;
	}
	*nspans = ns / 2;
	return spans;
}

/* Evaluate a buffered requires-expression.  `sp[0..n)` spans the whole
 * expression including the `requires` keyword.  Returns true when all
 * requirements are satisfied. */
static bool
cpp_requires_eval(struct scope *s, struct token *sp, size_t n)
{
	struct scope *rs;
	size_t i, nbody = 0;
	struct token *body;
	bool ok;

	if (n == 0)
		return false;
	/* `requires` keyword */
	i = 1;
	if (i < n && sp[i].kind == TLPAREN)
		/* cpp_req_params returns the sub-span offset just past the ')';
		 * the whole-span index is 1 (for the `requires` keyword) plus it */
		i = 1 + cpp_req_params(s, &sp[i], n - i);
	if (i >= n || sp[i].kind != TLBRACE)
		return false; /* malformed: no requirement body */
	++i; /* skip '{' */
	body = &sp[i];
	nbody = n - i - 1; /* exclude the trailing '}' */

	/* a trial scope for the parameters; discarded after the check */
	rs = mkscope(s);
	ok = true;
	if (nbody > 0) {
		size_t nspans, *spans = cpp_req_split(body, nbody, &nspans);
		size_t k;
		for (k = 0; k < nspans; k++) {
			struct token *rq = &body[spans[2 * k]];
			size_t rn = spans[2 * k + 1];
			bool r;
			if (rn == 0)
				continue;
			if (cpp_classify_token(rq[0]) == CPP_TTYPENAME)
				r = cpp_req_type_ok(rs, rq, rn);
			else if (rq[0].kind == TLBRACE)
				r = cpp_req_compound(rs, rq, rn);
			else if (cpp_classify_token(rq[0]) == CPP_TREQUIRES)
				/* nested requirement: `requires C<T>;` */
				r = eval_constraint(&rq[1], rn - 1, rs);
			else
				r = cpp_req_simple(rs, rq, rn);
			if (!r) {
				ok = false;
				break;
			}
		}
		free(spans);
	}
	delscope(rs);
	return ok;
}

/* Entry point: parse a requires-expression at the current token and
 * return its boolean constant value.  Consumes the whole expression.
 *
 * The expression is buffered as a token span, then the span is replayed
 * (pushed onto the token context) for evaluation under a trial.  On both
 * success and failure the token context is restored to its pre-replay
 * depth, so the caller (which has already consumed the requires-expression
 * tokens) resumes at the token after the expression regardless of what the
 * trial did. */
struct expr *
cpp_requires_expr(struct scope *s)
{
	struct token *sp;
	struct token after;
	size_t n, depth;
	bool result;
	struct expr *e;
	jmp_buf env;

	/* buffer the whole expression first so a failed check still leaves
	 * the stream positioned after it */
	n = cpp_requires_span_len(&sp);
	if (n == 0)
		error_code(E_CTYPE, &tok.loc, "malformed requires-expression");
	/* `after` is the first token past the requires-expression; the caller
	 * resumes here.  Captured before the trial, because a failed trial
	 * may leave the global token anywhere. */
	next(); /* advance past the closing '}' to the token after it */
	after = tok;
	depth = tokctx_depth();

	/* evaluate the buffered expression under a trial: replay the span,
	 * then restore the context; any parse/type error makes it false.
	 * cpp_trial_begin/end save and restore the enclosing trial's jump
	 * buffer (and depth) so a failed trial unwinds here with result false
	 * rather than rethrowing into the caller. */
	if (setjmp(env) == 0) {
		cpp_trial_begin(env);
		tokpush(sp, n);
		next();
		result = cpp_requires_eval(s, sp, n);
		cpp_trial_end(env);
	} else {
		/* an error was raised inside the trial; restore the trial
		 * bookkeeping and report the expression as false. */
		cpp_trial_end(env);
		result = false;
	}
	/* restore the stream to just past the requires-expression: discard
	 * the trial's replayed tokens and set the global token to the one
	 * after the expression (the trial never advanced the source stream,
	 * so `after` is still the next source token). */
	tokctx_rewind(depth);
	tok = after;
	free(sp);

	e = mkexpr(EXPRCONST, &typebool, NULL);
	e->u.constant.u = result;
	return e;
}


/* --- C++17 fold expressions ------------------------------------------- */

/* Is `k` a foldable binary operator?  Fold expressions permit the
 * arithmetic, logical, bitwise, comparison, shift, comma, and pointer
 * member operators.  We treat any token that is not punctuation/a type
 * and has a two-operand form as foldable; parens/brackets/braces and
 * terminators are excluded. */
static bool
cpp_fold_isop(enum tokenkind k)
{
	switch (k) {
	case TADD: case TSUB: case TMUL: case TDIV: case TMOD:
	case TBAND: case TBOR: case TXOR: case TSHL: case TSHR:
	case TLAND: case TLOR: /* && || (logical) */
	case TEQL: case TNEQ: case TLESS: case TGREATER:
	case TLEQ: case TGEQ:
	case TCOMMA:
		return true;
	default:
		return false;
	}
}

/* Emit `pack_var_k` (the k-th element of the expanded pack) into the
 * output buffer. */
static void
cpp_fold_emit_elt(struct token **out, size_t *n, size_t *cap,
                  struct token tpl, const char *pack_var, int k)
{
	if (*n >= *cap) {
		*cap = *cap ? *cap * 2 : 64;
		*out = xreallocarray(*out, *cap, sizeof **out);
	}
	char vn[32];
	struct token t = tpl;
	snprintf(vn, sizeof vn, "%s_%d", pack_var, k);
	t.kind = tokenget(vn, strlen(vn));
	(*out)[(*n)++] = t;
}

/* Append one raw token to the output buffer. */
static void
cpp_fold_emit(struct token **out, size_t *n, size_t *cap, struct token t)
{
	if (*n >= *cap) {
		*cap = *cap ? *cap * 2 : 64;
		*out = xreallocarray(*out, *cap, sizeof **out);
	}
	(*out)[(*n)++] = t;
}

/* Expand a unary fold `( ... op pack )` (left) or `( pack op ... )`
 * (right) into a fully-parenthesized chain of binary operations over the
 * `npack` pack elements, mirroring C++17 semantics:
 *   (... op pack):  ((a0 op a1) op a2) ... op a_{n-1}
 *   (pack op ...):  a0 op (a1 op (... op a_{n-1}))
 */
static void
cpp_fold_unary(struct token **out, size_t *n, size_t *cap,
               struct token tpl, enum tokenkind op,
               const char *pack_var, int npack, bool right)
{
	int k;
	if (right) {
		/* right fold, innermost first: emit `a0 op (a1 op (... a_k))` */
		cpp_fold_emit_elt(out, n, cap, tpl, pack_var, 0);
		for (k = 1; k < npack; ++k) {
			struct token ot = tpl; ot.kind = op;
			struct token lp = tpl; lp.kind = TLPAREN;
			cpp_fold_emit(out, n, cap, ot);
			cpp_fold_emit(out, n, cap, lp);
			cpp_fold_emit_elt(out, n, cap, tpl, pack_var, k);
		}
		for (k = 1; k < npack; ++k) {
			struct token rp = tpl; rp.kind = TRPAREN;
			cpp_fold_emit(out, n, cap, rp);
		}
	} else {
		/* left fold: `((a0 op a1) op a2) ... op a_{n-1}` */
		cpp_fold_emit_elt(out, n, cap, tpl, pack_var, 0);
		for (k = 1; k < npack; ++k) {
			struct token ot = tpl; ot.kind = op;
			cpp_fold_emit(out, n, cap, ot);
			cpp_fold_emit_elt(out, n, cap, tpl, pack_var, k);
		}
	}
}

/* Scan `toks[0..n)` for fold-expression shapes involving `pack_var` and
 * emit an expanded token stream into a heap buffer (row `*outn`). */
static void
cpp_expand_folds(struct token *toks, size_t n, const char *pack_var,
                 int npack, struct token **out, size_t *outn)
{
	struct token *res = NULL;
	size_t rn = 0, cap = 0;
	size_t i;
	(void)outn;

	for (i = 0; i < n;) {
		struct token t = toks[i];
		/* unary left fold: `( ... op pack )` */
		if (t.kind == TLPAREN && i + 4 < n &&
		    toks[i + 1].kind == TELLIPSIS &&
		    cpp_fold_isop(toks[i + 2].kind) &&
		    toks[i + 3].kind >= TIDENT &&
		    strcmp(tokenstr(toks[i + 3].kind), pack_var) == 0 &&
		    toks[i + 4].kind == TRPAREN) {
			cpp_fold_unary(&res, &rn, &cap, t,
			    toks[i + 2].kind, pack_var, npack, false);
			i += 5;
			continue;
		}
		/* unary right fold: `( pack op ... )` */
		if (t.kind == TLPAREN && i + 4 < n &&
		    toks[i + 1].kind >= TIDENT &&
		    strcmp(tokenstr(toks[i + 1].kind), pack_var) == 0 &&
		    cpp_fold_isop(toks[i + 2].kind) &&
		    toks[i + 3].kind == TELLIPSIS &&
		    toks[i + 4].kind == TRPAREN) {
			npack = npack > 0 ? npack : 1;
			cpp_fold_unary(&res, &rn, &cap, t,
			    toks[i + 2].kind, pack_var, npack, true);
			i += 5;
			continue;
		}
		/* binary fold, pack on the LEFT: `( pack op ... op init )` */
		if (t.kind == TLPAREN && i + 5 < n &&
		    toks[i + 1].kind >= TIDENT &&
		    strcmp(tokenstr(toks[i + 1].kind), pack_var) == 0 &&
		    cpp_fold_isop(toks[i + 2].kind) &&
		    toks[i + 3].kind == TELLIPSIS &&
		    cpp_fold_isop(toks[i + 4].kind)) {
			/* find the matching ')' — init is the tokens between
			 * the second op and the ')'. */
			size_t j;
			/* The pack must be on the left for this shape (the
			 * second operator runs before init).  Find init end. */
			for (j = i + 5; j < n; ++j)
				if (toks[j].kind == TRPAREN)
					break;
			if (j < n) {
				/* binary left fold:
				 *   ((... (pack_0 op pack_1) op ... op pack_{n-1}))
				 *      op init */
				cpp_fold_emit(&res, &rn, &cap,
				    (struct token){.kind = TLPAREN});
				cpp_fold_unary(&res, &rn, &cap, t,
				    toks[i + 2].kind, pack_var, npack, false);
				cpp_fold_emit(&res, &rn, &cap,
				    (struct token){.kind = TRPAREN});
				cpp_fold_emit(&res, &rn, &cap,
				    (struct token){.kind = toks[i + 4].kind});
				for (size_t k2 = i + 5; k2 < j; k2++)
					cpp_fold_emit(&res, &rn, &cap, toks[k2]);
				i = j + 1;
				continue;
			}
		}
		/* binary fold, pack on the RIGHT: `( init op ... op pack )` */
		if (t.kind == TLPAREN && i + 5 < n) {
			size_t ep; /* index of the '...' */
			for (ep = i + 1; ep + 2 < n; ++ep)
				if (toks[ep].kind == TELLIPSIS)
					break;
			/* shape: init op ... op pack ) — pack_var right after an
			 * op before the closing ')'. */
			if (ep + 3 < n && cpp_fold_isop(toks[ep - 1].kind) &&
			    cpp_fold_isop(toks[ep + 1].kind) &&
			    toks[ep + 2].kind >= TIDENT &&
			    strcmp(tokenstr(toks[ep + 2].kind), pack_var) == 0 &&
			    toks[ep + 3].kind == TRPAREN) {
				/* binary right fold:
				 *   init op (pack_0 op (pack_1 op (... op
				 *   pack_{n-1}))) */
				for (size_t k2 = i + 1; k2 < ep - 1; ++k2)
					cpp_fold_emit(&res, &rn, &cap, toks[k2]);
				cpp_fold_emit(&res, &rn, &cap,
				    (struct token){.kind = toks[ep - 1].kind});
				cpp_fold_unary(&res, &rn, &cap, t,
				    toks[ep + 1].kind, pack_var, npack, true);
				i = ep + 4;
				continue;
			}
		}
		cpp_fold_emit(&res, &rn, &cap, t);
		++i;
	}
	*out = res;
	*outn = rn;
}


/* Find the DECLFUNC for the instantiation of template `name` with the
 * given call-site arguments, instantiating it (replaying the buffered
 * declaration with each parameter bound to a concrete type) on first use. */
static struct decl *
cpp_tmpl_find_or_instantiate(struct scope *s, const char *name,
                             struct expr *arglist)
{
	struct cpp_template *tmpl;
	struct cpp_tmpl_inst *inst;
	struct cpp_tmpl_param *p;
	struct type *types[16];
	unsigned long long nttp_vals[16];
	char key[128], fnname[128];
	struct scope *bs;
	struct decl *fd, *td;
	struct token cur;
	int i, nt = 0;
	bool found;

	for (tmpl = g_cpp_templates; tmpl; tmpl = tmpl->next)
		if (strcmp(tmpl->name, name) == 0)
			break;
	if (!tmpl)
		return NULL;
	if (tmpl->nparams > 16)
		error_code(E_SYNTAX, &tok.loc, "template '%s' has too many parameters", name);
	if (!cpp_tmpl_deduce(tmpl, s, arglist, types, &nt, nttp_vals))
		error_code(E_TEMPLATE, &tok.loc, "too few arguments for template '%s'", name);

	/* mangled name: name + "_" + type codes / NTTP values (e.g. max_i,
	 * cpp20_nttp_42).  NTTP arguments are distinct by value, so the
	 * cache key must encode the value, not just the type. */
	snprintf(key, sizeof key, "%s", name);
	for (i = 0; i < nt; ++i) {
		char code[64];
		if (tmpl_param_is_nttp(tmpl, i))
			snprintf(code, sizeof code, "%llu", nttp_vals[i]);
		else
			cpp_mangle_type(types[i], code, sizeof code);
		strncat(key, "_", sizeof key - strlen(key) - 1);
		strncat(key, code, sizeof key - strlen(key) - 1);
	}
	snprintf(fnname, sizeof fnname, "%s", key);

	for (inst = tmpl->insts; inst; inst = inst->next)
		if (strcmp(inst->key, key) == 0)
			return inst->fn;

	/* instantiate: bind parameters as type names / constants, replay the
	 * declaration.  The buffered tokens are shared across instantiations,
	 * so rename on a private copy. */
	bs = mkscope(s);
	{
		int nfix = 0, npack = 0;
		/* fixed parameters bind positionally; a trailing parameter pack
		 * binds its first element as a placeholder (the per-element types
		 * are registered as __tp0..__tpN-1 for the pack expansion below) */
		for (p = tmpl->params, i = 0; p && !p->is_pack; p = p->next, ++i, ++nfix) {
			if (p->is_nttp) {
			/* non-type parameter: bind the constant value.
			 * u.enumconst feeds the constant evaluator, value feeds
			 * the IR emitter. */
			td = mkdecl((char *)p->name, DECLCONST,
			    p->nttp_type ? p->nttp_type : types[i],
			    QUALNONE, LINKNONE);
			td->u.enumconst = nttp_vals[i];
			td->value = mkintconst(nttp_vals[i]);
			} else {
				td = mkdecl((char *)p->name, DECLTYPE, types[i],
				    QUALNONE, LINKNONE);
			}
			scopeputdecl(bs, td);
		}
		if (p && p->is_pack) {
			npack = nt - nfix;
			td = mkdecl((char *)p->name, DECLTYPE,
			    nfix < nt ? types[nfix] : &typevoid, QUALNONE, LINKNONE);
			scopeputdecl(bs, td);
			for (i = 0; i < npack; ++i) {
				char *tname = xmalloc(32);
				snprintf(tname, 32, "__tp%d", i);
				td = mkdecl(tname, DECLTYPE, types[nfix + i], QUALNONE, LINKNONE);
				scopeputdecl(bs, td);
			}
			/* make the pack size visible to sizeof...(Args) while the
			 * replay below is parsed */
			if (g_cpp_pack_depth < (int)countof(g_cpp_pack_stack))
				g_cpp_pack_stack[g_cpp_pack_depth++] = npack;
		}
	}
	{
		struct token *rtoks = xmalloc(tmpl->ntoks * sizeof *rtoks);
		size_t ntoks = tmpl->ntoks;
		memcpy(rtoks, tmpl->toks, tmpl->ntoks * sizeof *rtoks);
		/* variadic pack expansion on a private copy:
		 *   `Args ... args` (a pack in the parameter list) becomes
		 *       `__tp0 args_0 , __tp1 args_1 , ...`
		 *   `args ...` (a pack used as call arguments) becomes
		 *       `args_0 , args_1 , ...` */
		{
			const char *ptname = NULL;
			const char *pack_var = NULL;
			int nfix = 0, npack;
			struct cpp_tmpl_param *pp;
			for (pp = tmpl->params; pp; pp = pp->next)
				if (pp->is_pack) {
					ptname = pp->name;
					break;
				}
			if (ptname) {
				struct token *wtoks = xmalloc((ntoks + 16 * 8 + 8) * sizeof *wtoks);
				size_t wn = 0;
				for (pp = tmpl->params; pp && !pp->is_pack; pp = pp->next)
					++nfix;
				npack = nt - nfix; /* pack element count */
				/* Discover the pack *variable* name (`args` in
				 * `template<...> auto f(A... args)`) by scanning for
				 * the parameter-list pack `ptname ... NAME`.  Needed
				 * by the fold-expression expansion below. */
				{
					size_t si;
					for (si = 0; si < ntoks; si++)
						if (rtoks[si].kind >= TIDENT &&
						    strcmp(tokenstr(rtoks[si].kind), ptname) == 0 &&
						    si + 2 < ntoks &&
						    rtoks[si + 1].kind == TELLIPSIS &&
						    rtoks[si + 2].kind >= TIDENT) {
							pack_var = tokenstr(rtoks[si + 2].kind);
							break;
						}
				}
				/* C++17 fold expressions over the pack: expand any
				 * `(... op pack)` / `(pack op ...)` / `(pack op ... op
				 * init)` shape into a fully-parenthesized binary chain
				 * over pack_0..pack_{n-1}. */
				if (pack_var && npack > 0) {
					struct token *folds = NULL;
					size_t fn = 0;
					cpp_expand_folds(rtoks, ntoks, pack_var, npack,
					    &folds, &fn);
					if (folds) {
						free(rtoks);
						rtoks = folds;
						ntoks = fn;
					}
				}
				for (i = 0; i < (int)ntoks; ++i) {
					const char *nm = rtoks[i].kind >= TIDENT
					    ? tokenstr(rtoks[i].kind) : NULL;
					if (nm && strcmp(nm, ptname) == 0 &&
					    i + 2 < (int)ntoks &&
					    rtoks[i + 1].kind == TELLIPSIS &&
					    rtoks[i + 2].kind >= TIDENT) {
						/* parameter-list pack: `Args ... args` */
						pack_var = tokenstr(rtoks[i + 2].kind);
						for (int k = 0; k < npack; ++k) {
							char tn[32], vn[32];
							struct token tt = rtoks[i];
							if (k) {
								tt.kind = TCOMMA;
								wtoks[wn++] = tt;
							}
							snprintf(tn, sizeof tn, "__tp%d", k);
							tt.kind = tokenget(tn, strlen(tn));
							wtoks[wn++] = tt;
							snprintf(vn, sizeof vn, "%s_%d", pack_var, k);
							tt.kind = tokenget(vn, strlen(vn));
							wtoks[wn++] = tt;
						}
						i += 2;
						continue;
					}
					if (pack_var && nm && strcmp(nm, pack_var) == 0 &&
					    i + 1 < (int)ntoks && rtoks[i + 1].kind == TELLIPSIS) {
						/* call-site pack: `args ...` */
						for (int k = 0; k < npack; ++k) {
							char vn[32];
							struct token vt = rtoks[i];
							if (k) {
								vt.kind = TCOMMA;
								wtoks[wn++] = vt;
							}
							snprintf(vn, sizeof vn, "%s_%d", pack_var, k);
							vt.kind = tokenget(vn, strlen(vn));
							wtoks[wn++] = vt;
						}
						++i;
						continue;
					}
					wtoks[wn++] = rtoks[i];
				}
				free(rtoks);
				rtoks = wtoks;
				ntoks = wn;
			}
		}
		/* rename the function token to the mangled instantiation name (the
		 * parser resolves identifiers by their interned token kind, so
		 * intern the mangled name and swap the kind) */
		found = false;
		for (i = 0; i < (int)ntoks; ++i) {
			const char *nm;
			if (rtoks[i].kind < TIDENT)
				continue;
			nm = tokenstr(rtoks[i].kind);
			if (cpp_classify_ident(nm, strlen(nm)) != CPP_TNONE)
				continue;
			bool is_param = false;
			for (p = tmpl->params; p; p = p->next)
				if (strcmp(p->name, nm) == 0) {
					is_param = true;
					break;
				}
			if (!is_param) {
				rtoks[i].kind = tokenget(fnname, strlen(fnname));
				found = true;
				break;
			}
		}
		if (!found)
			error_code(E_DECL, &tok.loc, "cannot locate function name in template '%s'", name);

		cur = tok;
		tokpush(&cur, 1);
		tokpush(rtoks, ntoks);
		/* rtoks stays alive (tokpush stores pointers) until decl() consumes
		 * it below; deliberately not freed (bounded per-instantiation). */
	}
	next();
	/* C++20 requires-clause: the constraint must hold for the deduced
	 * types, otherwise the instantiation is ill-formed. */
	{
		bool cok = cpp_check_constraint(tmpl, bs);
			if (!cok)
			error_code(E_TEMPLATE, &tok.loc,
			    "template '%s' instantiated with a type that does not satisfy its requires-clause",
			    name);
	}
	/* Expose this instantiation's template parameter type bindings so a
	 * constexpr body buffered inside decl() can capture them for the
	 * constant evaluator.  Saved and restored across nested instantiations
	 * (a constexpr template body may call another template) so the outer
	 * function keeps its own parameters. */
	{
		const char *sv_p[16];
		struct type *sv_t[16];
		unsigned long long sv_v[16];
		bool sv_iv[16];
		int sv_n = g_cpp_cexpr_tmpl_n, k, nfix = 0;
		struct cpp_tmpl_param *pp;
		for (k = 0; k < sv_n; ++k) {
			sv_p[k] = g_cpp_cexpr_tmpl_params[k];
			sv_t[k] = g_cpp_cexpr_tmpl_types[k];
			sv_v[k] = g_cpp_cexpr_tmpl_vals[k];
			sv_iv[k] = g_cpp_cexpr_tmpl_isval[k];
		}
		g_cpp_cexpr_tmpl_n = 0;
		/* count fixed (non-pack) parameters; the trailing pack binds its
		 * first element as a placeholder at this index */
		for (pp = tmpl->params; pp; pp = pp->next)
			if (!pp->is_pack)
				++nfix;
		{
			int pi = 0;
			for (pp = tmpl->params; pp && g_cpp_cexpr_tmpl_n < 16;
			     pp = pp->next, ++pi) {
				int ti = pp->is_pack ? nfix : pi;
				if (ti >= nt)
					break;
				g_cpp_cexpr_tmpl_params[g_cpp_cexpr_tmpl_n] = pp->name;
				g_cpp_cexpr_tmpl_types[g_cpp_cexpr_tmpl_n] = types[ti];
				if (pp->is_nttp) {
					g_cpp_cexpr_tmpl_isval[g_cpp_cexpr_tmpl_n] = true;
					g_cpp_cexpr_tmpl_vals[g_cpp_cexpr_tmpl_n] =
					    nttp_vals[ti];
				} else {
					g_cpp_cexpr_tmpl_isval[g_cpp_cexpr_tmpl_n] = false;
				}
				++g_cpp_cexpr_tmpl_n;
			}
		}
		if (!decl(bs, NULL))
			error_code(E_TEMPLATE, &tok.loc, "failed to instantiate template '%s'", name);
		g_cpp_cexpr_tmpl_n = sv_n;
		for (k = 0; k < sv_n; ++k) {
			g_cpp_cexpr_tmpl_params[k] = sv_p[k];
			g_cpp_cexpr_tmpl_types[k] = sv_t[k];
			g_cpp_cexpr_tmpl_vals[k] = sv_v[k];
			g_cpp_cexpr_tmpl_isval[k] = sv_iv[k];
		}
	}
	if (g_cpp_pack_depth > 0)
		--g_cpp_pack_depth;

	fd = scopegetdecl(bs, fnname, 1);
	if (!fd || fd->kind != DECLFUNC)
		error_code(E_DECL, &tok.loc, "template '%s' instantiation produced no function", name);
	/* re-register at file scope so later uses of the same instantiation
	 * (and cross-function calls) resolve the symbol */
	scopeputdecl(s, fd);
	delscope(bs);

	inst = xmalloc(sizeof(*inst));
	snprintf(inst->key, sizeof inst->key, "%s", key);
	inst->fn = fd;
	inst->next = NULL;
	*tmpl->insts_end = inst;
	tmpl->insts_end = &inst->next;
	return fd;
}

/* TLPAREN helper: when a pending template call is recorded (set by
 * cpp_tmpl_placeholder), instantiate the template from the argument types
 * and return the decayed callee expression; clears the pending state. */
struct expr *
cpp_tmpl_instantiate(struct scope *s, struct expr *arglist)
{
	const char *nm;
	struct decl *fd;
	struct expr *e;

	if (!g_cpp_tmpl_depth)
		return NULL;
	/* pop before instantiating: the replay of the template body parses
	 * nested calls whose TLPAREN lowering pops their own pending names */
	nm = g_cpp_tmpl_stack[--g_cpp_tmpl_depth];
	fd = cpp_tmpl_find_or_instantiate(s, nm, arglist);
	g_cpp_tmpl_expl_n = 0; /* explicit args are consumed by the instance */
	if (!fd)
		return NULL;
	e = mkexpr(EXPRIDENT, fd->type, NULL);
	e->u.ident.decl = fd;
	e->lvalue = false;
	return decay(e);
}

/* --- C.2.8 class templates (instantiate-on-first-use) ------------------ */

const char *
cpp_tmpl_class_lookup(const char *name)
{
	struct cpp_template *t;

	for (t = g_cpp_templates; t; t = t->next)
		if (t->is_class && strcmp(t->name, name) == 0)
			return t->name;
	return NULL;
}

/* Instantiate a class template with the given type arguments: replay the
 * buffered `class Foo { ... }` declaration (with each parameter bound and
 * the tag renamed to the mangled instantiation name `Foo_<codes>`) through
 * cpp_class_decl.  Returns the instantiated class type (cached per key). */
static struct type *
cpp_tmpl_class_do_inst(struct scope *s, struct cpp_template *tmpl,
                       struct type **args, unsigned long long *nttp)
{
	extern struct scope filescope;
	struct cpp_tmpl_cls_inst *ci;
	struct cpp_tmpl_param *p;
	struct type *t;
	char key[128], tag[128];
	struct token cur;
	struct decl *td;
	size_t depth;
	int i;

	/* mangled tag name: Foo + "_" + type codes / NTTP values (Foo_i,
	 * Arr_3) */
	snprintf(key, sizeof key, "%s", tmpl->name);
	for (i = 0; i < tmpl->nparams; ++i) {
		char code[64];
		if (tmpl_param_is_nttp(tmpl, i))
			snprintf(code, sizeof code, "%llu", nttp[i]);
		else
			cpp_mangle_type(args[i], code, sizeof code);
		strncat(key, "_", sizeof key - strlen(key) - 1);
		strncat(key, code, sizeof key - strlen(key) - 1);
	}
	snprintf(tag, sizeof tag, "%s", key);

	for (ci = tmpl->cls_insts; ci; ci = ci->next)
		if (strcmp(ci->key, key) == 0)
			return ci->t;

	/* bind the parameters as type names / constants (re-put replaces the
	 * previous binding; the names are generic template params and stay
	 * benignly in file scope) */
	g_cpp_tmpl_nbinds = 0;
	for (p = tmpl->params, i = 0; p; p = p->next, ++i) {
		if (p->is_nttp) {
			td = mkdecl((char *)p->name, DECLCONST,
			    p->nttp_type ? p->nttp_type : args[i],
			    QUALNONE, LINKNONE);
			td->u.enumconst = nttp[i];
			td->value = mkintconst(nttp[i]);
		} else {
			td = mkdecl((char *)p->name, DECLTYPE, args[i],
			    QUALNONE, LINKNONE);
		}
		scopeputdecl(&filescope, td);
		/* Snapshot this binding so a deferred method body can be
		 * re-bound when parsed later (see cpp_ensure_method_defined).
		 * `binds[16]` matches the template parameter limit enforced
		 * above (tmpl->nparams > 16 is rejected as a syntax error),
		 * so the buffer can never be overrun here. */
		if (g_cpp_tmpl_nbinds < 16)
			g_cpp_tmpl_binds[g_cpp_tmpl_nbinds++] = td;
	}
	/* C++20: a constrained type parameter (`template<Concept T> class`)
	 * must be satisfied by the instantiation's arguments. */
	if (!cpp_check_constraint(tmpl, &filescope))
		error_code(E_TEMPLATE, &tok.loc,
		    "class template '%s' instantiated with a type that does not satisfy its requires-clause",
		    tmpl->name);
	/* rename the class-name token to the mangled tag, and every
	 * constructor/destructor token (which spells the original class name)
	 * so struct_decl recognizes them as the class's own constructors.
	 * The buffered tokens are shared across instantiations, so rename on
	 * a private copy. */
	{
		/* append a trailing ';' so cpp_class_decl's replay does not run
		 * past the class definition into the caller's token stream (the
		 * template buffer stops at the closing '}' without it) */
		struct token *rtoks = xmalloc((tmpl->ntoks + 1) * sizeof *rtoks);
		bool found = false;
		int rn = tmpl->ntoks;
		memcpy(rtoks, tmpl->toks, tmpl->ntoks * sizeof *rtoks);
		rtoks[rn].kind = TSEMICOLON;
		rtoks[rn].space = false;
		++rn;
		for (i = 0; i < rn; ++i) {
			const char *nm;
			if (rtoks[i].kind < TIDENT)
				continue;
			nm = tokenstr(rtoks[i].kind);
			if (cpp_classify_ident(nm, strlen(nm)) != CPP_TNONE)
				continue;
			bool is_param = false;
			for (p = tmpl->params; p; p = p->next)
				if (strcmp(p->name, nm) == 0) {
					is_param = true;
					break;
				}
			if (!is_param && strcmp(nm, tmpl->name) == 0) {
				/* the class name itself or a constructor/destructor */
				rtoks[i].kind = tokenget(tag, strlen(tag));
				found = true;
				/* Injected-class-name written as a template-id
				 * (`Box<T>` inside `template<class T> struct Box`):
				 * the name is now the mangled tag, so the trailing
				 * `<...>` would be parsed as a stray less-than and
				 * break the declarator.  Drop the argument list —
				 * within its own definition `Box<T>` denotes exactly
				 * this instantiation.  Only elide when every argument
				 * token is one of the template's own parameters, so a
				 * genuine comparison (`Box_i < x`) is left alone. */
				if (i + 1 < rn && rtoks[i + 1].kind == TLESS) {
					int depth2 = 0, j, k;
					bool argsonly = true;
					for (j = i + 1; j < rn; ++j) {
						if (rtoks[j].kind == TLESS) {
							++depth2;
							continue;
						}
						if (rtoks[j].kind == TGREATER) {
							if (--depth2 == 0)
								break;
							continue;
						}
						if (rtoks[j].kind == TCOMMA)
							continue;
						if (rtoks[j].kind >= TIDENT) {
							const char *an = tokenstr(rtoks[j].kind);
							bool ok = false;
							for (p = tmpl->params; p; p = p->next)
								if (strcmp(p->name, an) == 0) {
									ok = true;
									break;
								}
							if (ok)
								continue;
						}
						argsonly = false;
						break;
					}
					if (argsonly && depth2 == 0 && j < rn) {
						/* splice out rtoks[i+1 .. j] (`<`..`>`) */
						int ndrop = j - i;
						for (k = i + 1; k + ndrop < rn; ++k)
							rtoks[k] = rtoks[k + ndrop];
						rn -= ndrop;
					}
				}
			}
		}
		if (!found)
			error_code(E_TEMPLATE, &tok.loc, "cannot locate class name in template '%s'", tmpl->name);

		/* replay `class Foo_i { ... }` to define the instantiated class.
		 * rtoks stays alive (tokpush stores pointers) until cpp_class_decl
		 * consumes it below; deliberately not freed (bounded).  Record the
		 * token-context depth so the replay's unconsumed tokens (class
		 * method bodies are buffered, not consumed) can be discarded
		 * afterwards instead of leaking into the caller's stream. */
		depth = tokctx_depth();
		cur = tok;
		tokpush(&cur, 1);
		tokpush(rtoks, rn);
	}
	next();
	{
		/* cpp_class_decl replays the class definition; constructor bodies
		 * are parsed by flush_pending_methods, which switches curfunc.
		 * Restore it so the caller (a declaration mid-parse, e.g. CTAD)
		 * keeps targeting the right function. */
		extern struct func *curfunc;
		struct func *saved_cf = curfunc;
		bool saved_inst = g_cpp_tmpl_instantiating;
		g_cpp_tmpl_instantiating = true;
		cpp_class_decl(&filescope);
		g_cpp_tmpl_instantiating = saved_inst;
		curfunc = saved_cf;
	}
	/* restore the caller's token position (the replayed definition has
	 * been consumed; cur was the caller's token pushed ahead of rtoks)
	 * and drop any unconsumed replay tokens */
	tok = cur;
	tokctx_rewind(depth);

	t = scopegettag(&filescope, tag, 1);
	if (!t)
		error_code(E_TEMPLATE, &tok.loc, "class template '%s' instantiation produced no class", tmpl->name);

	ci = xmalloc(sizeof(*ci));
	snprintf(ci->key, sizeof ci->key, "%s", key);
	ci->t = t;
	ci->next = NULL;
	*tmpl->cls_insts_end = ci;
	tmpl->cls_insts_end = &ci->next;
	return t;
}

/* Instantiate a class template: `Foo<...>` (tok positioned at '<').
 * Parses the explicit template arguments, then delegates to
 * cpp_tmpl_class_do_inst. */
struct type *
cpp_tmpl_class_instantiate(struct scope *s, const char *name)
{
	extern struct type *typename(struct scope *, enum typequal *,
	    struct expr **);
	struct cpp_template *tmpl;
	struct cpp_tmpl_param *p;
	struct type *args[16];
	unsigned long long nttp_vals[16];
	char key[128], tag[128];
	struct token cur;
	struct decl *td;
	enum typequal tq;
	struct expr *toeval;
	int i, n = 0;

	for (tmpl = g_cpp_templates; tmpl; tmpl = tmpl->next)
		if (tmpl->is_class && strcmp(tmpl->name, name) == 0)
			break;
	if (!tmpl)
		return NULL;
	if (tmpl->nparams > 16)
		error_code(E_SYNTAX, &tok.loc, "template '%s' has too many parameters", name);

	/* explicit template arguments `<T1, 42, ...>`: types for type
	 * parameters, constant expressions for non-type parameters.  An
	 * empty `<>` (C++11 `Box<>` with a defaulted first parameter) and
	 * omitted trailing arguments fall back to the defaults recorded in
	 * each parameter's deftoks. */
	expect(TLESS, "after class template name");
	while (tok.kind != TGREATER) {
		if (n >= tmpl->nparams)
			error_code(E_SYNTAX, &tok.loc, "too many template arguments for class template '%s'", name);
		tq = QUALNONE;
		toeval = NULL;
		if (tmpl_param_is_nttp(tmpl, n)) {
			struct expr *ev = cpp_tmpl_const_arg(s);
			args[n] = ev->type;
			nttp_vals[n] = ev->u.constant.u;
		} else {
			args[n] = typename(s, &tq, &toeval);
		}
		++n;
		if (tok.kind == TGREATER)
			break;
		expect(TCOMMA, "',' or '>' in class template argument list");
	}
	next(); /* consume '>' */
	/* apply default template arguments for the remaining parameters */
	if (n < tmpl->nparams) {
		extern struct scope *mkscope(struct scope *);
		extern void scopeputdecl(struct scope *, struct decl *);
		extern struct decl *mkdecl(char *, enum declkind,
		    struct type *, enum typequal, enum linkage);
		extern struct expr *condexpr(struct scope *);
		extern struct expr *eval(struct expr *);
		extern void tokpush(struct token *, size_t);
		struct scope *ds = mkscope(s);
		struct cpp_tmpl_param *pp;
		struct token post_args = tok; /* restore the stream after '>' */
		int j;
		/* bind the already-resolved params (defaults may reference an
		 * earlier type parameter) */
		for (pp = tmpl->params, j = 0; pp && j < n; pp = pp->next, ++j) {
			if (pp->is_nttp)
				scopeputdecl(ds, mkdecl((char *)pp->name,
				    DECLCONST, args[j], QUALNONE, LINKNONE));
			else
				scopeputdecl(ds, mkdecl((char *)pp->name,
				    DECLTYPE, args[j], QUALNONE, LINKNONE));
		}
		for (; n < tmpl->nparams; ++n, pp = pp ? pp->next : NULL) {
			if (!pp || !pp->deftoks || pp->ndeftoks == 0)
				error_code(E_TEMPLATE, &tok.loc,
				    "too few template arguments for class template '%s'", name);
			size_t d = tokctx_depth();
			struct token guard2 = {0};
			guard2.kind = TSEMICOLON;
			tokpush(&guard2, 1);
			tokpush(pp->deftoks, pp->ndeftoks);
			next();
			if (pp->is_nttp) {
				struct expr *ev = eval(condexpr(ds));
				tokctx_rewind(d);
				tok = post_args;
				if (!ev || ev->kind != EXPRCONST ||
				    !(ev->type->prop & PROPINT))
					error_code(E_TEMPLATE, &pp->deftoks[0].loc,
					    "default template argument is not a constant integer expression");
				args[n] = ev->type;
				nttp_vals[n] = ev->u.constant.u;
			} else {
				enum typequal dq = QUALNONE;
				struct expr *dtoe = NULL;
				struct type *dt = typename(ds, &dq, &dtoe);
				tokctx_rewind(d);
				tok = post_args;
				if (!dt || dtoe)
					error_code(E_TEMPLATE, &pp->deftoks[0].loc,
					    "default template argument is not a type");
				args[n] = dt;
			}
		}
	}

	(void)p;
	(void)key;
	(void)tag;
	(void)cur;
	(void)td;
	return cpp_tmpl_class_do_inst(s, tmpl, args, nttp_vals);
}

/* C++17 CTAD: `Vec v(a, b)` — a class template used without explicit
 * arguments deduces its template parameters from the constructor-call
 * argument types (each argument maps positionally to one parameter,
 * unwrapping references/pointers to the value type).  Looks up the
 * template by `name` and instantiates it with the deduced arguments. */
struct type *
cpp_tmpl_class_ctad(struct scope *s, const char *name, struct expr *args)
{
	struct cpp_template *tmpl;
	struct cpp_tmpl_param *p;
	struct type *types[16];
	unsigned long long nttp_vals[16];
	struct expr *a;
	int i = 0;

	for (tmpl = g_cpp_templates; tmpl; tmpl = tmpl->next)
		if (tmpl->is_class && strcmp(tmpl->name, name) == 0)
			break;
	if (!tmpl)
		return NULL;
	if (tmpl->nparams > 16)
		error_code(E_SYNTAX, &tok.loc, "template '%s' has too many parameters", name);

	/* deduce one template argument per call argument, positionally */
	for (p = tmpl->params, a = args; a && i < tmpl->nparams;
	    a = a->next, ++i, p = p ? p->next : NULL) {
		struct type *at = a->type;
		if (p && p->is_nttp) {
			/* NTTP via CTAD is not deduced from a value here;
			 * the constructor argument's constant value would be
			 * needed — fall back to its type (rare in practice). */
			nttp_vals[i] = 0;
			types[i] = at;
			continue;
		}
		/* unwrap pointer to the pointee for `T *d` parameters */
		while (at && at->kind == TYPEPOINTER && !at->isref)
			at = at->base;
		if (!at)
			at = &typeint;
		types[i] = at;
	}
	if (i < tmpl->nparams)
		error_code(E_TEMPLATE, &tok.loc,
		    "cannot deduce template argument %d of '%s' (too few constructor arguments)",
		    i + 1, name);

	return cpp_tmpl_class_do_inst(s, tmpl, types, nttp_vals);
}

/* --- C.2.8 member templates (template methods in a class) ------------- */

/* Explicit template arguments `<int>` recorded by cpp_tmpl_member_pend;
 * consumed by cpp_tmpl_member_instantiate. */
static struct type *g_cpp_tmpl_member_args[16];
static int g_cpp_tmpl_member_nargs;

/* Is `name` a template member function of class `t`?  Member templates
 * are registered during class-body parsing (owner == the class type). */
bool
cpp_tmpl_member(struct type *t, const char *name)
{
	struct cpp_template *tmpl;

	if (!t || !name)
		return false;
	for (tmpl = g_cpp_templates; tmpl; tmpl = tmpl->next)
		if (tmpl->is_member && tmpl->owner == t &&
		    strcmp(tmpl->name, name) == 0)
			return true;
	return false;
}

/* Dummy callee expression for a pending member-template call (satisfies
 * the TLPAREN "called object" checks until the instantiation replaces
 * it). */
struct expr *
cpp_tmpl_member_placeholder(void)
{
	struct expr *e;

	e = mkexpr(EXPRIDENT, mkpointertype(cpp_tmpl_dummy_callee()->type,
	    QUALNONE), NULL);
	e->u.ident.decl = cpp_tmpl_dummy_callee();
	e->lvalue = false;
	return e;
}

/* Record a pending member-template call.  `tok` is positioned on the
 * member name; it is consumed, along with the optional explicit template
 * argument list `<T1, T2>` (the caller has confirmed the member is a
 * template).  After this returns, `tok` is positioned on the '(' of the
 * call. */
void
cpp_tmpl_member_pend(struct type *t, const char *name)
{
	extern struct type *typename(struct scope *, enum typequal *,
	    struct expr **);
	extern struct scope filescope;

	g_cpp_member_class = t;
	g_cpp_member_name = name;
	g_cpp_member_tmpl = true;
	g_cpp_tmpl_member_nargs = 0;

	next(); /* consume the member name */
	if (tok.kind != TLESS)
		return; /* no explicit template arguments: deduce from args */
	next(); /* consume '<' */
	while (tok.kind != TGREATER) {
		enum typequal tq = QUALNONE;
		struct expr *toeval = NULL;
		if (g_cpp_tmpl_member_nargs >= (int)countof(g_cpp_tmpl_member_args))
			error_code(E_SYNTAX, &tok.loc, "too many explicit template arguments");
		g_cpp_tmpl_member_args[g_cpp_tmpl_member_nargs++] =
		    typename(&filescope, &tq, &toeval);
		if (tok.kind == TGREATER)
			break;
		expect(TCOMMA, "',' or '>' in template argument list");
	}
	next(); /* consume '>' */
}

/* Instantiate the pending member-template call (`obj.get<int>(...)`):
 * bind the template parameters (explicit `<...>` args first, the rest
 * deduced positionally from the call arguments), replay the buffered
 * declaration with the method name mangled to `Class_method_codes`, and
 * define it as a member function (with the implicit `this`).  Returns the
 * decayed callee expression for the instantiated function. */
struct expr *
cpp_tmpl_member_instantiate(struct scope *s, struct expr *thisp,
                            struct expr *arglist)
{
	extern struct qualtype declspecs(struct scope *, enum storageclass *,
	    enum funcspec *, int *);
	extern struct qualtype declarator(struct scope *, struct qualtype,
	    char **, int *, struct scope **, bool, struct attr *);
	extern struct scope *delscope(struct scope *);

	struct cpp_template *tmpl;
	struct cpp_tmpl_inst *inst;
	struct cpp_tmpl_param *p;
	struct type *types[16];
	char mname[128], key[128], sym[256];
	struct scope *bs;
	struct decl *fd, *td;
	struct token cur;
	struct expr *e;
	int i, n;
	bool found;
	struct type *owner;
	const char *name;
	const char *tag;

	owner = g_cpp_member_class;
	name = g_cpp_member_name;
	(void)thisp;
	if (!owner || !name)
		return NULL;
	tag = owner->u.structunion.tag;
	if (!tag)
		error_code(E_TEMPLATE, &tok.loc, "member template of an unnamed class is not supported");

	for (tmpl = g_cpp_templates; tmpl; tmpl = tmpl->next)
		if (tmpl->is_member && tmpl->owner == owner &&
		    strcmp(tmpl->name, name) == 0)
			break;
	if (!tmpl)
		error_code(E_DECL, &tok.loc, "no template member function '%s' in class '%s'",
		    name, tag);
	if (tmpl->nparams > 16)
		error_code(E_SYNTAX, &tok.loc, "template member '%s' has too many parameters",
		    name);

	/* explicit `<...>` args fill the leading parameters; the rest are
	 * deduced positionally from the call-site argument types */
	n = g_cpp_tmpl_member_nargs;
	for (i = 0; i < n; ++i)
		types[i] = g_cpp_tmpl_member_args[i];
	{
		struct expr *a;
		for (a = arglist; a && i < tmpl->nparams; a = a->next, ++i)
			types[i] = a->type;
	}
	if (i < tmpl->nparams)
		error_code(E_TEMPLATE, &tok.loc,
		    "too few template arguments for template member '%s'", name);

	/* mangled member name + cache key: `get_i` / `Wrapper_get_i` (keyed
	 * on the template type codes; the full symbol — with the explicit
	 * parameter codes that cpp_define_method appends — is built after
	 * the declarator is parsed). */
	snprintf(mname, sizeof mname, "%s", name);
	for (i = 0; i < tmpl->nparams; ++i) {
		char code[64];
		cpp_mangle_type(types[i], code, sizeof code);
		strncat(mname, "_", sizeof mname - strlen(mname) - 1);
		strncat(mname, code, sizeof mname - strlen(mname) - 1);
	}
	snprintf(key, sizeof key, "%s_%s", tag, mname);
	snprintf(sym, sizeof sym, "%s", key);

	for (inst = tmpl->insts; inst; inst = inst->next)
		if (strcmp(inst->key, key) == 0) {
			e = mkexpr(EXPRIDENT, inst->fn->type, NULL);
			e->u.ident.decl = inst->fn;
			e->lvalue = false;
			return decay(e);
		}

	/* instantiate: bind parameters as type names, replay the buffered
	 * declaration with the method token renamed to the mangled name.
	 * The declaration is parsed with declspecs/declarator and routed to
	 * cpp_define_method (which adds the implicit `this` parameter), so
	 * the replayed `T get_i() {...}` defines `Wrapper_get_i`.
	 * Scope base: the owner class's declaration scope (not the call-site
	 * scope `s`) so the method body resolves captures/members via the
	 * class (cpp_member_ident) instead of seeing the caller's locals. */
	bs = mkscope(owner->scope ? owner->scope : s);
	for (p = tmpl->params, i = 0; p; p = p->next, ++i) {
		td = mkdecl((char *)p->name, DECLTYPE, types[i], QUALNONE, LINKNONE);
		scopeputdecl(bs, td);
	}
	{
		int rn = tmpl->ntoks;
		struct token *rtoks = xmalloc(rn * sizeof *rtoks);
		memcpy(rtoks, tmpl->toks, rn * sizeof *rtoks);
		found = false;
		for (i = 0; i < rn; ++i) {
			const char *nm;
			if (rtoks[i].kind < TIDENT)
				continue;
			nm = tokenstr(rtoks[i].kind);
			/* `operator` overload template: `template<typename T> auto
			 * operator()(...)`.  Replace `operator` + its trailing token
			 * (e.g. `()`) with the mangled method name so declarator sees
			 * a plain `mname(...)` declarator. */
			if (cpp_classify_ident(nm, strlen(nm)) == CPP_TOPERATOR) {
				rtoks[i].kind = tokenget(mname, strlen(mname));
				found = true;
				/* `operator()` is `operator` `(` `)`: drop both so the
				 * mangled name becomes a plain `mname(...)` declarator;
				 * a token-operator (`operator+`) is just `operator` `+`
				 * — drop the single operand too. */
				if (i + 1 < rn && rtoks[i + 1].kind == TLPAREN) {
					int drop = 2; /* '(' ')' */
					if (i + 2 < rn && rtoks[i + 2].kind == TRPAREN)
						memmove(rtoks + i + 1, rtoks + i + 3,
						    (rn - i - 3) * sizeof *rtoks);
					else
						memmove(rtoks + i + 1, rtoks + i + 2,
						    (rn - i - 2) * sizeof *rtoks);
					rn -= drop;
				}
				break;
			}
			if (cpp_classify_ident(nm, strlen(nm)) != CPP_TNONE)
				continue;
			bool is_param = false;
			for (p = tmpl->params; p; p = p->next)
				if (strcmp(p->name, nm) == 0) {
					is_param = true;
					break;
				}
			if (!is_param) {
				rtoks[i].kind = tokenget(mname, strlen(mname));
				found = true;
				break;
			}
		}
		if (!found)
			error_code(E_TEMPLATE, &tok.loc, "cannot locate member name in template '%s'", name);
		cur = tok;
		tokpush(&cur, 1);
		tokpush(rtoks, rn);
	}
	next();
	{
		struct qualtype base, mt;
		enum storageclass sc = SCNONE;
		enum funcspec fs = 0;
		char *dname;
		int align = 0;

		base = declspecs(bs, &sc, &fs, &align);
		if (!base.type)
			error_code(E_CTYPE, &tok.loc, "no type in template member declaration");
		mt = declarator(bs, base, &dname, &align, NULL, false, NULL);
		if (mt.type->kind != TYPEFUNC)
			error_code(E_DECL, &tok.loc, "template member '%s' is not a function", name);
		/* a trailing `const` (e.g. a lambda's const operator()) is a
		 * member cv-qualifier, not part of the C declarator grammar:
		 * consume it here and pass it through so the const overload
		 * (`Class_methodK<args>`) is instantiated, matching the
		 * non-template member path (struct_decl.c). */
		bool mconst = false;
		if (tok.kind == TCONST) {
			mconst = true;
			next();
		}
		cpp_define_method(bs, mt.type, mname, tag, mconst,
		    (sc & SCSTATIC) != 0, false);
		/* cpp_define_method appends the encoded explicit parameter types
		 * to the mangled symbol (`Wrapper_add_i` + `i` -> `Wrapper_add_ii`),
		 * so build the full symbol name the same way for the lookup. */
		{
			struct decl *cur;
			snprintf(sym, sizeof sym, "%s_%s", tag, mname);
			if (mconst)
				strncat(sym, "K", sizeof sym - strlen(sym) - 1);
			for (cur = mt.type->u.func.params; cur; cur = cur->next) {
				char code[64];
				cpp_mangle_type(cur->type, code, sizeof code);
				strncat(sym, code, sizeof sym - strlen(sym) - 1);
			}
		}
	}
	delscope(bs);

	/* cpp_define_method registered the mangled function in the class's
	 * declaration scope */
	fd = scopegetdecl(owner->scope ? owner->scope : s, sym, 1);
	if (!fd || fd->kind != DECLFUNC)
		error_code(E_DECL, &tok.loc, "template member '%s' instantiation produced no function", name);

	inst = xmalloc(sizeof(*inst));
	snprintf(inst->key, sizeof inst->key, "%s", key);
	inst->fn = fd;
	inst->next = NULL;
	*tmpl->insts_end = inst;
	tmpl->insts_end = &inst->next;

	e = mkexpr(EXPRIDENT, fd->type, NULL);
	e->u.ident.decl = fd;
	e->lvalue = false;
	return decay(e);
}

/* One captured variable of a lambda (`x` in `[x]`). */
struct cpp_lambda_cap {
	const char *name;      /* capture name (also the closure member name) */
	struct type *t;        /* captured variable's type */
	struct decl *d;        /* the enclosing-scope variable decl (NULL when
	                          the capture resolves to an outer closure's
	                          member — defect T) */
	struct expr *arg;      /* lvalue of the captured entity: the local
	                          variable's identifier, or `(*this).m` for an
	                          outer closure member */
	bool by_ref;           /* `[&x]` reference capture */
};

/* Token-stream builder for the synthesized closure-class definition. */
static void
cpp_tb(struct token *buf, size_t *n, struct token tmpl, enum tokenkind k,
       const char *name)
{
	struct token *t = &buf[*n];
	*t = tmpl;
	if (name)
		t->kind = tokenget(name, strlen(name));
	else
		t->kind = k;
	++*n;
}

/* Should a captured value be copy-constructed through its class's
 * constructor rather than bit-copied?  A class-typed capture (`[c]` where
 * `c` is a class object with a user constructor) must run the copy ctor —
 * defect S — otherwise construction side effects (refcounts, deep copies,
 * logging) are silently lost.  Such captures are initialized via a ctor
 * initializer-list item `c(__cN)` so the member-init path selects the
 * copy/move ctor by overload resolution; scalars and classes without a
 * user ctor keep the plain bitwise body assignment. */
static bool
cpp_lambda_cap_needs_ctor_init(const struct cpp_lambda_cap *cap)
{
	struct type *t = cap->t;
	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return false;
	return cpp_has_ctor(t, t->u.structunion.tag);
}

/* Is `t` the closure class of a lambda (`__lambdaN`)?  A no-capture
 * closure is an empty, constant-constructible object, so its closure
 * object may serve as a static/constant initializer — file-scope
 * `auto f = [](...){...};` and `constexpr auto f = [](...){...};`. */
bool
cpp_is_lambda_closure(const struct type *t)
{
	const char *tag;

	if (!t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return false;
	tag = t->u.structunion.tag;
	return tag && strncmp(tag, "__lambda", 8) == 0;
}

/* Gather the default captures of a lambda: every automatic object
 * visible in the enclosing scopes (from `s` up to, but excluding, the
 * file scope) that is not already in `caps[0..*ncap)`, appended either
 * by reference (`[&]`) or by value (`[=]`).  A reference variable
 * `T &r` is captured by reference as `T &` (of the referent) or by
 * value as `T` (the referent's value). */
static void
cpp_lambda_default_captures(struct scope *s, struct cpp_lambda_cap *caps,
    int *ncap, int capmax, bool by_ref)
{
	extern struct scope filescope;
	struct scope *sc;

	for (sc = s; sc && sc != &filescope; sc = sc->parent) {
		struct map *m = &sc->decls;
		size_t i;
		if (!m->len)
			continue;
		for (i = 0; i < m->cap; ++i) {
			struct decl *d;
			struct expr *ie;
			bool seen = false;
			int j;

			if (!m->keys[i].str)
				continue;
			d = m->vals[i].p;
			if (!d || d->kind != DECLOBJECT)
				continue;
			if (d->linkage != LINKNONE || d->u.obj.storage != SDAUTO)
				continue;
			/* the implicit this parameter is not capturable */
			if (d->name && strcmp(d->name, "this") == 0)
				continue;
			/* a default *by-value* capture skips lambda closure objects: the
			 * synthesized closure class has no copy ctor, so `[=]` cannot
			 * copy an enclosing lambda.  (`[&]` may still capture its
			 * reference.) */
			if (!by_ref && d->type && cpp_is_lambda_closure(d->type))
				continue;
			for (j = 0; j < *ncap; ++j)
				if (strcmp(caps[j].name, d->name) == 0) {
					seen = true;
					break;
				}
			if (seen)
				continue;
			if (*ncap >= capmax)
				error_code(E_SYNTAX, &tok.loc,
				    "too many captures in lambda");
			caps[*ncap].name = d->name;
			caps[*ncap].d = d;
			caps[*ncap].by_ref = by_ref;
			ie = mkexpr(EXPRIDENT, d->type, NULL);
			ie->qual = d->qual;
			ie->lvalue = true;
			ie->u.ident.decl = d;
			if (by_ref && d->type && d->type->isref) {
				/* `[&]` on a reference variable captures the referent:
				 * member `T &`, ctor arg `&*r` */
				caps[*ncap].t = d->type;
				caps[*ncap].arg = mkunaryexpr(TMUL, ie);
				caps[*ncap].arg->lvalue = true;
			} else if (by_ref) {
				caps[*ncap].t = mkpointertype(d->type, QUALNONE);
				caps[*ncap].t->isref = true; /* T & */
				caps[*ncap].arg = ie;
			} else if (d->type && d->type->isref) {
				/* `[=]` on a reference variable captures the referent's
				 * value */
				caps[*ncap].t = d->type->base;
				caps[*ncap].arg = mkunaryexpr(TMUL, ie);
				caps[*ncap].arg->lvalue = true;
			} else {
				caps[*ncap].t = d->type;
				caps[*ncap].arg = ie;
			}
			++*ncap;
		}
	}
}

/* Parse a C++11 lambda expression `[captures](params) -> ret { body }` and
 * lower it to an anonymous closure class (`__lambdaN`) whose `operator()`
 * is the lambda body and whose members are the by-value captures; returns
 * a freshly constructed closure object (an anonymous temporary).
 *
 * The closure class is defined by replaying a synthesized
 * `class __lambdaN { ... }` through cpp_class_decl, reusing the existing
 * member/constructor/operator machinery.  By-reference captures
 * (`[&x]`, `[&]`), default by-value captures (`[=]`), init-captures
 * (`[n = expr]`) and mixed captures (`[=, &y]`, `[&, y]`) are
 * supported; generic (auto) parameters use the same mechanism. */
struct expr *
cpp_lambda_expr(struct scope *s)
{
	extern struct func *curfunc;
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);
	extern void funcinit(struct func *, struct decl *, struct init *,
	    bool);
	extern void tokpush(struct token *, size_t);

	struct cpp_lambda_cap caps[32];
	int ncap = 0;
	struct token *ptoks = NULL, *rtoks = NULL, *btoks = NULL;
	size_t pn = 0, pcap = 0, rn = 0, rcap = 0, bn = 0, bcap = 0;
	/* C++20 lambda template parameter list: `[]<typename T>(T x) { ... }`.
	 * Buffered like the parameter list, with `<`/`>` nesting for templates
	 * that themselves contain template parameters (e.g. `template<typename T,
	 * template<typename> class Container>`). */
	struct token *ttoks = NULL;
	size_t tn2 = 0, tcap = 0;
	struct token *wtoks;
	size_t wn = 0, wcap;
	struct token tmpl = tok;
	char tagname[64], tn[32], cn[32];
	struct token cur;
	struct decl *td;
	struct type *ct;
	struct expr *e, *args, **ae;
	struct decl *tmp;
	int i;

	/* --- capture list `[ x, &y, &, =, n = expr ]` ---
	 * C++11/14 captures:
	 *   `[x]`        by-value capture of local x
	 *   `[&x]`       by-reference capture of local x
	 *   `[&]`        default by-reference capture (all enclosing locals)
	 *   `[=]`        default by-value capture (all enclosing locals)
	 *   `[n = expr]` init-capture: new closure member n from expr
	 *   `[&n = expr]` reference init-capture
	 *   `[=, &y]` / `[&, y]`  default plus per-variable override
	 *
	 * A by-reference capture lowers as: the closure member is declared
	 * `T &` (cpp_member_ident auto-dereferences it inside the operator()
	 * body), the synthesized ctor takes a plain `T *` parameter (so it
	 * does not auto-dereference), the ctor binds it through the
	 * initializer-list item `name(__cN)` (`*(this + off) = __cN`), and
	 * the closure object is constructed with `&x`. */
	next(); /* consume '[' */
	{
		bool def_ref = false, def_val = false, lead_ref = false;

		/* leading default-capture specifier: `[&]` / `[=]`, optionally
		 * followed by explicit captures (`[&, y]`, `[=, &y]`).  When the
		 * specifier is directly followed by a capture name (`[&x]`) it is
		 * a by-reference capture of that variable instead. */
		if (tok.kind == TBAND || tok.kind == TASSIGN) {
			bool br = tok.kind == TBAND;
			next();
			if (tok.kind == TRBRACK || tok.kind == TCOMMA) {
				def_ref = br;
				def_val = !br;
				if (tok.kind == TCOMMA)
					next();
			} else {
				if (!br)
					error_code(E_SYNTAX, &tok.loc,
					    "expected ']' or ',' after '=' in lambda capture list");
				lead_ref = true; /* `[&name`: by-reference capture */
			}
		}
		while (tok.kind != TRBRACK) {
			bool by_ref = lead_ref;
			lead_ref = false;
			if (tok.kind == TBAND) {
				by_ref = true;
				next();
				if (tok.kind == TRBRACK || tok.kind == TCOMMA)
					error_code(E_SYNTAX, &tok.loc,
					    "expected capture name after '&' in lambda capture list");
			}
			if (tok.kind < TIDENT)
				error_code(E_SYNTAX, &tok.loc,
				    "expected capture name in lambda capture list");
			if (ncap >= (int)countof(caps))
				error_code(E_SYNTAX, &tok.loc, "too many captures in lambda");
			caps[ncap].name = tokenstr(tok.kind);
			caps[ncap].by_ref = by_ref;
			next();
			if (tok.kind == TASSIGN) {
				/* init-capture `[n = expr]` / `[&n = expr]`: the name is not
				 * an enclosing variable — it introduces a new closure member
				 * initialized from the expression. */
				next(); /* consume '=' */
				caps[ncap].d = NULL;
				caps[ncap].arg = assignexpr(s);
				if (by_ref) {
					caps[ncap].t = mkpointertype(caps[ncap].arg->type,
					    QUALNONE);
					caps[ncap].t->isref = true; /* T & */
				} else {
					caps[ncap].t = caps[ncap].arg->type;
				}
				++ncap;
			} else {
				caps[ncap].d = scopegetdecl(s, caps[ncap].name, 1);
				if (caps[ncap].d && caps[ncap].d->kind == DECLOBJECT) {
					/* ordinary capture of an enclosing-scope local */
					struct decl *d = caps[ncap].d;
					if (by_ref && d->type && d->type->isref) {
						/* `[&r]` where `r` is `T &`: capture the referent —
						 * member `T &`, ctor arg `&*r` */
						struct expr *ie = mkexpr(EXPRIDENT, d->type, NULL);
						ie->qual = d->qual;
						ie->lvalue = true;
						ie->u.ident.decl = d;
						caps[ncap].t = d->type;
						caps[ncap].arg = mkunaryexpr(TMUL, ie);
						caps[ncap].arg->lvalue = true;
					} else if (by_ref) {
						caps[ncap].t = mkpointertype(d->type, QUALNONE);
						caps[ncap].t->isref = true; /* T & */
						caps[ncap].arg = mkexpr(EXPRIDENT, d->type, NULL);
						caps[ncap].arg->qual = d->qual;
						caps[ncap].arg->lvalue = true;
						caps[ncap].arg->u.ident.decl = d;
					} else if (d->type && d->type->isref) {
						/* `[r]` on a reference variable captures the
						 * referent's value */
						struct expr *ie = mkexpr(EXPRIDENT, d->type, NULL);
						ie->qual = d->qual;
						ie->lvalue = true;
						ie->u.ident.decl = d;
						caps[ncap].t = d->type->base;
						caps[ncap].arg = mkunaryexpr(TMUL, ie);
						caps[ncap].arg->lvalue = true;
					} else {
						caps[ncap].t = d->type;
						caps[ncap].arg = mkexpr(EXPRIDENT, d->type, NULL);
						caps[ncap].arg->qual = d->qual;
						caps[ncap].arg->lvalue = true;
						caps[ncap].arg->u.ident.decl = d;
					}
				} else {
					/* defect T: an outer lambda's captured variable lowers to
					 * a member of its closure class, not a local; resolve it
					 * the same way a bare member name in the operator() body
					 * is resolved — as `(*this).name` of the current method's
					 * class.  This lets an inner lambda re-capture anything the
					 * outer one captured. */
					struct expr *me = cpp_member_ident(s, caps[ncap].name);
					if (!me || me->type->kind == TYPEFUNC) {
						/* a member function (or non-member) is not capturable;
						 * clear any pending member-call state cpp_member_ident
						 * may have recorded */
						g_cpp_member_this = NULL;
						g_cpp_member_class = NULL;
						g_cpp_member_name = NULL;
						g_cpp_member_const = false;
						error_code(E_DECL, &tok.loc,
						    "cannot capture variable '%s'", caps[ncap].name);
					}
					caps[ncap].d = NULL;
					if (by_ref) {
						caps[ncap].t = mkpointertype(me->type, QUALNONE);
						caps[ncap].t->isref = true; /* T & */
					} else {
						caps[ncap].t = me->type;
					}
					caps[ncap].arg = me;
				}
				++ncap;
			}
			if (tok.kind == TRBRACK)
				break;
			expect(TCOMMA, "',' or ']' in lambda capture list");
		}
		next(); /* consume ']' */

		/* default captures `[&]` / `[=]`: gather every automatic object of
		 * the enclosing scopes that was not explicitly captured above. */
		if (def_ref || def_val)
			cpp_lambda_default_captures(s, caps, &ncap,
			    (int)countof(caps), def_ref);
	}

	/* --- C++20 lambda template parameter list `[captures]<typename T>(T x)`
	 * (optional; buffer through the matching '>') --- */
	if (tok.kind == TLESS) {
		int tdepth = 0;
		for (;;) {
			if (tn2 >= tcap) {
				tcap = tcap ? tcap * 2 : 16;
				ttoks = xreallocarray(ttoks, tcap, sizeof *ttoks);
			}
			ttoks[tn2++] = tok;
			if (tok.kind == TLESS)
				++tdepth;
			else if (tok.kind == TGREATER) {
				--tdepth;
				if (tdepth == 0) {
					next();
					break;
				}
			}
			next();
		}
	}

	/* --- parameter list `( params )` (optional in C++; buffer through
	 * the matching ')') --- */
	if (tok.kind == TLPAREN) {
		int pdepth = 0;
		for (;;) {
			if (pn >= pcap) {
				pcap = pcap ? pcap * 2 : 16;
				ptoks = xreallocarray(ptoks, pcap, sizeof *ptoks);
			}
			ptoks[pn++] = tok;
			if (tok.kind == TLPAREN)
				++pdepth;
			else if (tok.kind == TRPAREN) {
				--pdepth;
				if (pdepth == 0) {
					next();
					break;
				}
			}
			next();
		}
	} /* else: `[] { ... }` / `[] -> ret { ... }` — empty parameter list */

	/* --- optional `-> ret` return type (buffer up to the body) --- */
	if (tok.kind == TARROW) {
		next(); /* consume '->' */
		while (tok.kind != TLBRACE && tok.kind != TEOF) {
			if (rn >= rcap) {
				rcap = rcap ? rcap * 2 : 16;
				rtoks = xreallocarray(rtoks, rcap, sizeof *rtoks);
			}
			rtoks[rn++] = tok;
			next();
		}
	}

	/* --- function body `{ ... }` --- */
	{
		int bd = 0;
		if (tok.kind != TLBRACE)
			error_code(E_SYNTAX, &tok.loc, "expected lambda body");
		for (;;) {
			if (bn >= bcap) {
				bcap = bcap ? bcap * 2 : 32;
				btoks = xreallocarray(btoks, bcap, sizeof *btoks);
			}
			btoks[bn++] = tok;
			if (tok.kind == TLBRACE)
				++bd;
			else if (tok.kind == TRBRACE) {
				--bd;
				if (bd == 0) {
					next();
					break;
				}
			}
			next();
		}
	}

	/* --- synthesize the closure class `class __lambdaN { ... };` ---
	 * Defined at file scope (like a real C++ closure type) so the
	 * operator() body's name lookup does not see the enclosing function's
	 * locals (which would shadow the captured members). */
	{
		extern struct scope filescope;
		snprintf(tagname, sizeof tagname, "__lambda%d", g_cpp_lambda_count++);
		for (i = 0; i < ncap; ++i) {
			/* per-capture DECLTYPE `__lti` bound to the captured type;
			 * the closure member declaration `__lti cap_i` is typed.  A
			 * by-reference capture additionally gets `__ltpi` bound to
			 * `T *` for the synthesized ctor's parameter. */
			snprintf(tn, sizeof tn, "__lt%d", i);
			td = mkdecl(xmalloc(strlen(tn) + 1), DECLTYPE, caps[i].t,
			    QUALNONE, LINKNONE);
			strcpy((char *)td->name, tn);
			scopeputdecl(&filescope, td);
			if (caps[i].by_ref) {
				snprintf(tn, sizeof tn, "__ltp%d", i);
				td = mkdecl(xmalloc(strlen(tn) + 1), DECLTYPE,
				    mkpointertype(caps[i].t->base, QUALNONE),
				    QUALNONE, LINKNONE);
				strcpy((char *)td->name, tn);
				scopeputdecl(&filescope, td);
			}
		}
	}
	wcap = 64 + (size_t)ncap * 32 + pn + rn + bn + 32;
	wtoks = xmalloc(wcap * sizeof *wtoks);

	cpp_tb(wtoks, &wn, tmpl, 0, "class");
	cpp_tb(wtoks, &wn, tmpl, 0, tagname);
	cpp_tb(wtoks, &wn, tmpl, TLBRACE, NULL);
	cpp_tb(wtoks, &wn, tmpl, 0, "public");
	cpp_tb(wtoks, &wn, tmpl, TCOLON, NULL);
	for (i = 0; i < ncap; ++i) {
		snprintf(tn, sizeof tn, "__lt%d", i);
		cpp_tb(wtoks, &wn, tmpl, 0, tn);
		cpp_tb(wtoks, &wn, tmpl, 0, caps[i].name);
		cpp_tb(wtoks, &wn, tmpl, TSEMICOLON, NULL);
	}
	/* synthesized constructor
	 * `__lambdaN(__lt0 __c0, ...) : classcap(__c0) { scalarcap = __c1; }`
	 * Class-typed captures with a user constructor go through the ctor
	 * initializer list so the member-init path runs the copy/move ctor
	 * chosen by overload resolution (defect S); scalars and POD captures
	 * keep the plain assignment (a bit copy is their correct semantics). */
	cpp_tb(wtoks, &wn, tmpl, 0, tagname);
	cpp_tb(wtoks, &wn, tmpl, TLPAREN, NULL);
	for (i = 0; i < ncap; ++i) {
		if (i)
			cpp_tb(wtoks, &wn, tmpl, TCOMMA, NULL);
		/* by-reference captures take a plain `T *` parameter so it does
		 * not auto-dereference when bound by the ctor init list */
		snprintf(tn, sizeof tn, caps[i].by_ref ? "__ltp%d" : "__lt%d", i);
		cpp_tb(wtoks, &wn, tmpl, 0, tn);
		snprintf(cn, sizeof cn, "__c%d", i);
		cpp_tb(wtoks, &wn, tmpl, 0, cn);
	}
	cpp_tb(wtoks, &wn, tmpl, TRPAREN, NULL);
	{
		bool first = true;
		for (i = 0; i < ncap; ++i) {
			/* by-reference captures must bind in the init list too: a
			 * reference member can only be initialized, never assigned */
			if (!cpp_lambda_cap_needs_ctor_init(&caps[i]) &&
			    !caps[i].by_ref)
				continue;
			cpp_tb(wtoks, &wn, tmpl, first ? TCOLON : TCOMMA, NULL);
			first = false;
			cpp_tb(wtoks, &wn, tmpl, 0, caps[i].name);
			cpp_tb(wtoks, &wn, tmpl, TLPAREN, NULL);
			snprintf(cn, sizeof cn, "__c%d", i);
			cpp_tb(wtoks, &wn, tmpl, 0, cn);
			cpp_tb(wtoks, &wn, tmpl, TRPAREN, NULL);
		}
	}
	cpp_tb(wtoks, &wn, tmpl, TLBRACE, NULL);
	for (i = 0; i < ncap; ++i) {
		if (cpp_lambda_cap_needs_ctor_init(&caps[i]) || caps[i].by_ref)
			continue; /* already initialized by the init list */
		cpp_tb(wtoks, &wn, tmpl, 0, caps[i].name);
		cpp_tb(wtoks, &wn, tmpl, TASSIGN, NULL);
		snprintf(cn, sizeof cn, "__c%d", i);
		cpp_tb(wtoks, &wn, tmpl, 0, cn);
		cpp_tb(wtoks, &wn, tmpl, TSEMICOLON, NULL);
	}
	cpp_tb(wtoks, &wn, tmpl, TRBRACE, NULL);
	/* `operator()(params) { body }` — `ret` is the explicit `->` type if
	 * given, otherwise `auto` (deduced from the body's return).  A C++14
	 * generic lambda (`[](auto x) {...}`) has an `auto` parameter: the
	 * operator() becomes a function template (`template<typename __T0>
	 * operator()(__T0 x)`) so each call-site argument type instantiates
	 * its own version.  A C++20 explicit template parameter list
	 * (`[]<typename T>(T x)`) is emitted before the auto-parameter
	 * template (if any). */
	{
		bool generic = false;
		int i;
		for (i = 0; i < (int)pn; ++i)
			if (ptoks[i].kind == TAUTO)
				generic = true;
		/* C++20 explicit template parameter list, e.g. `<typename T>`. */
		if (tn2) {
			cpp_tb(wtoks, &wn, tmpl, 0, "template");
			/* emit the buffered tokens (starting with '<', ending with '>') */
			memcpy(wtoks + wn, ttoks, tn2 * sizeof *ttoks);
			wn += tn2;
		}
		if (generic) {
			cpp_tb(wtoks, &wn, tmpl, 0, "template");
			cpp_tb(wtoks, &wn, tmpl, TLESS, NULL);
			cpp_tb(wtoks, &wn, tmpl, 0, "typename");
			cpp_tb(wtoks, &wn, tmpl, 0, "__T0");
			cpp_tb(wtoks, &wn, tmpl, TGREATER, NULL);
		}
	}
	if (rn) {
		memcpy(wtoks + wn, rtoks, rn * sizeof *rtoks);
		wn += rn;
	} else {
		cpp_tb(wtoks, &wn, tmpl, 0, "auto");
	}
	cpp_tb(wtoks, &wn, tmpl, 0, "operator");
	cpp_tb(wtoks, &wn, tmpl, TLPAREN, NULL);
	cpp_tb(wtoks, &wn, tmpl, TRPAREN, NULL);
	if (pn) {
		/* copy the parameter tokens, replacing `auto` with the template
		 * type parameter `__T0` (generic lambda) */
		size_t i;
		for (i = 0; i < pn; ++i) {
			if (ptoks[i].kind == TAUTO)
				cpp_tb(wtoks, &wn, tmpl, 0, "__T0");
			else
				wtoks[wn++] = ptoks[i];
		}
	}
	/* the lambda's operator() is const by default (C++11 [expr.prim.lambda]p5,
	 * unless `mutable`): a const closure object — e.g. a `constexpr` or
	 * `const` lambda variable — must be callable through the const
	 * overload (`operator_clK...`), and by-value captures remain readable
	 * through a const `this`. */
	cpp_tb(wtoks, &wn, tmpl, 0, "const");
	if (bn) {
		memcpy(wtoks + wn, btoks, bn * sizeof *btoks);
		wn += bn;
	}
	cpp_tb(wtoks, &wn, tmpl, TRBRACE, NULL); /* close class body */
	cpp_tb(wtoks, &wn, tmpl, TSEMICOLON, NULL);

	/* replay the synthesized definition through cpp_class_decl.  The
	 * closure's operator() body is parsed by flush_pending_methods, which
	 * clobbers curfunc; restore it so the construction below (and the
	 * enclosing function's parsing) still targets the right function. */
	{
		extern struct scope filescope;
		struct func *saved_cur = curfunc;
		cur = tok;
		tokpush(&cur, 1);
		tokpush(wtoks, wn);
		next();
		cpp_class_decl(&filescope);
		curfunc = saved_cur;
	}

	ct = scopegettag(&filescope, tagname, 1);
	if (!ct)
		error_code(E_TEMPLATE, &tok.loc, "lambda closure class '%s' was not created", tagname);

	/* --- construct the closure object (anonymous temporary) --- */
	tmp = mkdecl("tmp", DECLOBJECT, ct, QUALNONE, LINKNONE);
	args = NULL;
	ae = &args;
	for (i = 0; i < ncap; ++i) {
		/* the capture's lvalue: a local variable's identifier, or an
		 * outer closure member `(*this).m` (defect T).  A by-reference
		 * capture passes the address (`T *` ctor parameter). */
		struct expr *cap = caps[i].arg;
		if (caps[i].by_ref)
			cap = mkunaryexpr(TBAND, cap);
		*ae = cap;
		ae = &cap->next;
	}
	if (!curfunc) {
		/* file-scope lambda: the closure object has static storage.
		 * A no-capture closure is an empty object — constant-
		 * constructible — so it needs no runtime construction; the
		 * closure object folds to a constant, which makes file-scope
		 * `auto f = [](...){...};` a valid static initializer and
		 * `constexpr auto f = [](...){...};` satisfy the constant-
		 * initializer requirement.  Capturing lambdas at file scope
		 * would require dynamic initialization (deferred to
		 * __mxx_global_var_init) and are not supported yet. */
		tmp->u.obj.storage = SDSTATIC;
		tmp->value = mkglobal(tmp);
		if (ncap == 0) {
			tmp->u.obj.constval = 0;
			tmp->u.obj.has_constval = true;
		} else {
			error_code(E_DECL, &tok.loc,
			    "file-scope lambda with captures is not supported yet");
		}
	} else {
		tmp->u.obj.storage = SDAUTO;
		funcinit(curfunc, tmp, NULL, false); /* allocate storage */
		cpp_emit_ctor_call(curfunc, tmp, args);
	}

	e = mkexpr(EXPRIDENT, ct, NULL);
	e->lvalue = true;
	e->u.ident.decl = tmp;
	return e;
}

