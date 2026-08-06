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
void cpp_template_decl(struct scope *s, struct type *owner);
bool cpp_try_abbrev_decl(struct scope *s);
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
	cpp_emit_exc_thunks();
}


/* --- C.2.8 function templates (instantiate-on-first-use) --------------- */

struct cpp_template *g_cpp_templates;
struct cpp_template **g_cpp_templates_end = &g_cpp_templates;
/* Pending template-call names, set by cpp_tmpl_placeholder in primaryexpr
 * and consumed by the TLPAREN lowering after the arguments are known.  A
 * stack so nested template calls in arguments (e.g. f(g(...))) each keep
 * their own pending name. */
const char *g_cpp_tmpl_stack[64];
int g_cpp_tmpl_depth;
/* Explicit template arguments parsed after a template name (`f<int, 42>`):
 * a leading type argument list that fills the template parameters before
 * any call-site deduction.  Each slot records whether it is a value (NTTP)
 * or a type.  Cleared after each instantiation. */
struct type *g_cpp_tmpl_expl_types[16];
unsigned long long g_cpp_tmpl_expl_vals[16];
bool g_cpp_tmpl_expl_isval[16];
int g_cpp_tmpl_expl_n;
/* Parameter-pack element counts of templates being replayed (pushed by the
 * variadic instantiation, popped when the replay completes).  `sizeof...`
 * consults the innermost count. */
int g_cpp_pack_stack[64];
int g_cpp_pack_depth;

/* Template parameter type bindings for the instantiation currently being
 * parsed into a constexpr body (set by cpp_tmpl_find_or_instantiate just
 * before decl(), consumed and cleared by cpp_buffer_constexpr_body). */
const char *g_cpp_cexpr_tmpl_params[16];
struct type *g_cpp_cexpr_tmpl_types[16];
unsigned long long g_cpp_cexpr_tmpl_vals[16];
bool g_cpp_cexpr_tmpl_isval[16];
int g_cpp_cexpr_tmpl_n;
