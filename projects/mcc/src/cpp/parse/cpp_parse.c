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
struct cpp_method_ctx g_cpp_method;

/* Pending qualified-class name from a `Class::method` declarator; consumed
 * by decl()'s DECLFUNC path to route out-of-line method definitions. */
static const char *g_cpp_qual_class;
/* Namespace the qualified class lives in (`ns::Class::method`), or NULL
 * for a plain file-scope `Class::method`. */
static struct scope *g_cpp_qual_ns;

static void emit_base_ctors_for(struct func *f, struct type *classt,
                                struct expr *thisp);
static void emit_base_dtors_for(struct func *f, struct type *classt,
                                struct expr *thisp);
static bool has_base(struct type *t);
static void cpp_template_decl(struct scope *s, struct type *owner);
static struct decl *cpp_tmpl_find_or_instantiate(struct scope *s,
    const char *name, struct expr *arglist);
static bool cpp_try_abbrev_decl(struct scope *s);
/* Set by cpp_define_method when the method just defined is effectively
 * virtual (explicit `virtual` or an override of a base virtual); consumed
 * by structdecl to flag the member. */
/* Set by cpp_define_method when the method just defined is effectively
 * virtual (explicit `virtual` or an override of a base virtual); consumed
 * by structdecl to flag the member. */
bool g_cpp_define_virtual;
/* Set when a method was declared with the `final` specifier. */
bool g_cpp_method_final;

/* Constructor init-list state (g_cpp_init_items, typed cpp_init_item):
 * used by the ctor lowering in cpp_ctor.c. */
struct cpp_init_item *g_cpp_init_items;
struct cpp_init_item **g_cpp_init_end = &g_cpp_init_items;

/* Two-phase class parsing: while a class body is being collected, method
 * bodies are buffered (tokens) and replayed after the class layout is
 * known, so a method body may reference members declared later. */
bool g_cpp_class_parsing;

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
bool cpp_class_decl(struct scope *s);
static void cpp_namespace_decl(struct scope *s);

/* Namespaces made visible by `using namespace NAME;` directives and by
 * `inline namespace` blocks.  Lookups that fail in the current scope
 * consult these before giving up. */
static struct scope *g_cpp_visible_ns[16];
static int g_cpp_nvisible_ns;
static void cpp_add_visible_ns(struct scope *ns);

static bool cpp_is_namespace_decl(void);
static void cpp_inherit_ctor(struct scope *s, struct type *derived,
                             struct structbuilder *b);
static void cpp_synth_inherited_ctor(struct scope *s, struct type *derived,
    struct structbuilder *b, struct type *base, struct type *bctor);

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
enum cpp_tokenkind
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
bool
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
void
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
struct member *
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


/* --- C.2.8 function templates (instantiate-on-first-use) --------------- */

struct cpp_template *g_cpp_templates;
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
int g_cpp_lambda_count;

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
struct decl *
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
struct expr *cpp_tmpl_const_arg(struct scope *s);

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

struct expr *
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
bool
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
