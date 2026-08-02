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
#include "../../parse/decl_internal.h"
#include "../../parse/expr_internal.h"

/* Pending `this` object for the next member-function call
 * (set by the postfix `.`/`->` lowering, consumed by TLPAREN). */
struct expr *g_cpp_member_this;
/* The class and method name of the pending member call, used by TLPAREN
 * to resolve overloads from the actual argument types. */
struct type *g_cpp_member_class;
const char *g_cpp_member_name;
bool g_cpp_member_const;
/* The pending member call is a template-member call (`obj.get<int>(...)`);
 * set by cpp_tmpl_member_pend, cleared with the rest of the pending state. */
bool g_cpp_member_tmpl;
/* C++14 `auto` return type deduction: while parsing the body of a function
 * whose declared return type is `auto`, the first return statement fixes
 * the deduced type and later returns must be compatible.  Set/reset around
 * each function body by the decl / method-body parsers. */
struct type *g_cpp_auto_ret_type;
struct func *g_cpp_auto_ret_func;
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
		g_cpp_member_tmpl = false;
		g_cpp_pending_placeholder = false;
	}
}

bool
cpp_pending_is_mine(int depth)
{
	return g_cpp_pending_depth == depth;
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
		error(&tok.loc, "unable to deduce the return type of an 'auto' function");
	if (g_cpp_auto_ret_type && !typecompatible(g_cpp_auto_ret_type, et))
		error(&tok.loc, "inconsistent deduced return types in 'auto' function");
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
static void cpp_mangle_type(struct type *t, char *buf, size_t bufsz);
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
/* Set by cpp_define_method when the method just defined is effectively
 * virtual (explicit `virtual` or an override of a base virtual); consumed
 * by structdecl to flag the member. */
bool g_cpp_define_virtual;

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

/* Build the expression for the implicit `this` pointer of the method
 * body currently being parsed (NULL outside a method body). */
static struct expr *
cpp_this_expr(void)
{
	struct expr *e;

	if (!g_cpp_method.active || !g_cpp_method.this_decl)
		return NULL;
	e = mkexpr(EXPRIDENT, g_cpp_method.this_decl->type, NULL);
	e->qual = g_cpp_method.this_decl->qual;
	e->lvalue = true;
	e->u.ident.decl = g_cpp_method.this_decl;
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
		error(&tok.loc, "request for member '%s' is ambiguous "
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
	return CPP_TNONE;
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
		error(&tok.loc, "expected class name");
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
				error(&tok.loc, "expected base class name after ':'");
			if (nbases >= (int)countof(bases))
				error(&tok.loc, "too many base classes");
			bases[nbases] = scopegettag(s, tokenstr(tok.kind), true);
			if (!bases[nbases] ||
			    (bases[nbases]->kind != TYPESTRUCT && bases[nbases]->kind != TYPEUNION))
				error(&tok.loc, "'%s' is not a class type", tokenstr(tok.kind));
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
			error(&tok.loc, "redeclaration of tag '%s' with different kind", tag);
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
		error(&tok.loc, "redefinition of class '%s'", tag);
	{
		int bi;
		for (bi = 0; bi < nbases; ++bi)
			if (bases[bi]->incomplete)
				error(&tok.loc, "base class '%s' has incomplete type",
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
			if (k == CPP_TCLASS) {
				/* nested class definition */
				cpp_class_decl(s);
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

/* Parse a C++ `namespace NAME { ... }` block.  Inner declarations are
 * registered in a fresh scope named NAME; the namespace itself is
 * registered in the enclosing scope as a DECLNAMESPACE so `NAME::symbol`
 * can be resolved (single-level for now).  The namespace scope outlives
 * parsing (never delscope'd) so qualified lookups keep working. */
static void
cpp_namespace_decl(struct scope *s)
{
	struct scope *ns;
	struct decl *nd;
	const char *name;

	next(); /* consume 'namespace' */
	if (tok.kind < TIDENT)
		error(&tok.loc, "expected namespace name");
	name = tokenstr(tok.kind);
	next();
	expect(TLBRACE, "after namespace name");

	ns = mkscope(s);
	ns->name = name;
	nd = mkdecl((char *)name, DECLNAMESPACE, NULL, QUALNONE, LINKNONE);
	nd->u.ns = ns;
	scopeputdecl(s, nd);

	while (tok.kind != TRBRACE && tok.kind != TEOF) {
		enum cpp_tokenkind k = cpp_tok_kind();
		if (k == CPP_TNAMESPACE) {
			cpp_namespace_decl(ns);
			continue;
		}
		if (k == CPP_TCLASS) {
			cpp_class_decl(ns);
			continue;
		}
		if (tok.kind == TSEMICOLON) {
			next();
			continue;
		}
		if (!decl(ns, NULL))
			error(&tok.loc, "expected declaration in namespace body");
	}
	next(); /* consume '}' */
	/* deliberately keep ns alive for later NAME::name lookups */
}

/* Namespaces made visible by `using namespace NAME;` directives.  Lookups
 * that fail in the current scope consult these before giving up. */
static struct scope *g_cpp_visible_ns[16];
static int g_cpp_nvisible_ns;

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

/* `using namespace NAME;` or `using NAME::member;`. */
static void
cpp_using_decl(struct scope *s)
{
	next(); /* consume 'using' */
	if (cpp_tok_kind() == CPP_TNAMESPACE) {
		struct decl *nsd;
		next(); /* consume 'namespace' */
		if (tok.kind < TIDENT)
			error(&tok.loc, "expected namespace name after 'using namespace'");
		nsd = scopegetdecl(s, tokenstr(tok.kind), 1);
		if (!nsd || nsd->kind != DECLNAMESPACE)
			error(&tok.loc, "'%s' is not a namespace", tokenstr(tok.kind));
		cpp_add_visible_ns(nsd->u.ns);
		next();
		expect(TSEMICOLON, "after using directive");
		return;
	}
	/* using NAME::member; */
	{
		struct decl *nsd;
		if (tok.kind < TIDENT)
			error(&tok.loc, "expected namespace name in using declaration");
		nsd = scopegetdecl(s, tokenstr(tok.kind), 1);
		next();
		expect(TCOLONCOLON, "after namespace name in using declaration");
		if (tok.kind < TIDENT)
			error(&tok.loc, "expected member name after '::'");
		if (!nsd || nsd->kind != DECLNAMESPACE)
			error(&tok.loc, "'%s' is not a namespace", nsd ? nsd->name : "?");
		{
			struct decl *md = scopegetdecl(nsd->u.ns, tokenstr(tok.kind), 1);
			if (!md)
				error(&tok.loc, "no member named '%s' in namespace '%s'",
				      tokenstr(tok.kind), nsd->name);
			scopeputdecl(s, md);
		}
		next();
		expect(TSEMICOLON, "after using declaration");
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

	while (tok.kind != TEOF) {
		/* C++ class/struct/union with access control */
		enum cpp_tokenkind k = cpp_tok_kind();
		if (k == CPP_TCLASS) {
			cpp_class_decl(&filescope);
			continue;
		}
		if (k == CPP_TNAMESPACE) {
			cpp_namespace_decl(&filescope);
			continue;
		}
		if (k == CPP_TUSING) {
			cpp_using_decl(&filescope);
			continue;
		}
		if (k == CPP_TTEMPLATE) {
			cpp_template_decl(&filescope, NULL);
			continue;
		}

		if (!decl(&filescope, NULL)) {
			if (tok.kind == TSEMICOLON)
				error(&tok.loc, "unexpected ';' at top-level");
			error(&tok.loc, "expected declaration or function definition");
		}
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
	struct cpp_pending_method *next;
};

static struct cpp_pending_method *g_cpp_pending_methods;
static struct cpp_pending_method **g_cpp_pending_methods_end =
    &g_cpp_pending_methods;

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
	extern void emitfunc(struct func *, bool);
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
	emitfunc(f, true);
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
	emitfunc(f, true);
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
	extern void emitfunc(struct func *, bool);
	extern void funchlt(struct func *);
	extern struct scope *delscope(struct scope *);

	struct scope *fs;
	struct decl *nd;
	struct func *f;

	fs = mkscope(pm->s);
	for (nd = pm->mtype->u.func.params; nd; nd = nd->next)
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
		/* constructor: run base-class constructors before the body */
		if (strcmp(pm->mname, pm->tag) == 0) {
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
				error(&tok.loc, "'auto' member function '%s' has no return statement to deduce its type from", pm->mname);
			pm->d->type->base = g_cpp_auto_ret_type;
			g_cpp_auto_ret_type = NULL;
			g_cpp_auto_ret_func = NULL;
		}
		emitfunc(f, pm->d->linkage == LINKEXTERN);
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
	pm->next = NULL;

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
		error(&tok.loc, "'%s' is not a class type", qclass);
	d = scopegetdecl(ct->scope ? ct->scope : &filescope, name, 1);
	if (!d || d->kind != DECLOBJECT) {
		error(&tok.loc, "no static data member '%s' in class '%s'",
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

/* Lower `l op r` to a member operator call `l.operator_pl(r)` when the
 * left operand is a class type with that operator overloaded.  Returns
 * true and sets *out on success (caller keeps normal arithmetic). */
bool
cpp_try_operator_call(struct scope *s, struct expr *l, enum tokenkind op,
                      struct expr *r, struct expr **out)
{
	extern struct scope filescope;

	struct type *t = l ? l->type : NULL;
	const char *opcode;
	char mname[64], mangled[256];
	struct decl *fd;
	struct expr *fn, *obj, *call, **end;

	opcode = cpp_op_mangle(op);
	if (!opcode || !t || (t->kind != TYPESTRUCT && t->kind != TYPEUNION))
		return false;
	snprintf(mname, sizeof mname, "operator_%s", opcode);
	if (cpp_is_member_function(t, mname)) {
		cpp_mangled_name_args(t, mname, r, mangled, sizeof mangled, false);
		fd = scopegetdecl(t->scope ? t->scope : &filescope, mangled, 1);
		if (!fd || fd->kind != DECLFUNC)
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
			*end = exprassign(r, fd->type->u.func.params->next->type);
			end = &(*end)->next;
			++call->u.call.nargs;
		}
		*out = call;
		return true;
	}
	/* non-member operator overload: `operator_pl(a, b)` registered as a
	 * free function in the current scope */
	extern struct scope filescope;
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
	*end = exprassign(l, fd->type->u.func.params->type);
	end = &(*end)->next;
	++call->u.call.nargs;
	if (r) {
		*end = exprassign(r,
		    fd->type->u.func.params->next
		    ? fd->type->u.func.params->next->type : NULL);
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
	e->lvalue = true;
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
	if (!m || m->access == ACC_PUBLIC)
		return true;
	if (m->access == ACC_PROTECTED && cpp_is_derived(g_cpp_method.class_type, t))
		return true;
	return cpp_same_class_context(t);
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
		error(&tok.loc, "'%s' is not a class type", class_tag);

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

	snprintf(mangled, sizeof mangled, "%s_%s", class_tag, mname);
	/* const member functions get a distinct mangled name so a const
	 * object can only call const methods */
	if (is_const)
		strncat(mangled, "K", sizeof mangled - strlen(mangled) - 1);
	/* overload resolution: append the encoded explicit parameter types
	 * (`Class_method_ii`); no-arg methods keep the bare mangled name */
	for (cur = funct->u.func.params; cur; cur = cur->next) {
		char code[64];
		cpp_mangle_type(cur->type, code, sizeof code);
		strncat(mangled, code, sizeof mangled - strlen(mangled) - 1);
	}
	/* static members get a distinct mangled name (no `this`); the S goes
	 * after the parameter encoding to match cpp_mangled_name_args + "S" */
	if (is_static)
		strncat(mangled, "S", sizeof mangled - strlen(mangled) - 1);
	/* mkdecl/scopeputdecl keep the name pointer; persist it off the
	 * stack (the C parser's token strings are stable, ours is not). */
	pmangled = xmalloc(strlen(mangled) + 1);
	strcpy(pmangled, mangled);

	/* Build the mangled function type:
	 * `Class_method(Class *this, args...) -> funct->base` (or just
	 * `Class_method(args...)` for a static member).  The declarator
	 * already parsed the explicit params into funct; we copy those decls
	 * so funct (kept in the member list for call lowering) and mtype
	 * don't share the same decl chain. */
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
		thisd = mkdecl("this", DECLOBJECT,
		               mkpointertype(classt, is_const ? QUALCONST : QUALNONE),
		               QUALNONE, LINKNONE);
		thisd->u.obj.storage = SDAUTO;
		*end = thisd;
		end = &thisd->next;
		++mtype->u.func.nparam;
	}
	for (cur = funct->u.func.params; cur; cur = cur->next) {
		nd = mkdecl(cur->name, DECLOBJECT, cur->type, cur->qual, LINKNONE);
		nd->u.obj.storage = SDAUTO;
		*end = nd;
		end = &nd->next;
		++mtype->u.func.nparam;
	}

	/* Register the mangled function symbol in the class's scope (the
	 * namespace scope for `namespace n { class C { ... }; }`) so the
	 * call lowering (postfixexpr TPERIOD) can resolve it from the
	 * object's class type. */
	{
		struct scope *ms = classt->scope ? classt->scope : s;
		d = scopegetdecl(ms, mangled, false);
		if (d && d->kind != DECLFUNC)
			error(&tok.loc, "'%s' redeclared with different kind", mangled);
		if (d && d->type && !typecompatible(mtype, d->type))
			error(&tok.loc, "'%s' redeclared with incompatible type", mangled);
		if (d && d->defined)
			error(&tok.loc, "redefinition of member function '%s'", mangled);
		if (!d) {
			d = mkdecl(pmangled, DECLFUNC, mtype, QUALNONE, LINKEXTERN);
			scopeputdecl(ms, d);
		} else {
			d->type = typecomposite(mtype, d->type);
			free(pmangled);
		}
	}
	d->value = mkglobal(d);

	if (tok.kind != TLBRACE) {
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
	extern void emitfunc(struct func *, bool);
	extern struct scope *delscope(struct scope *);

	const char *opcode;
	char mname[64], *pmangled;
	struct type *ft;
	struct decl *pd, *d, **pend;
	struct decl *nd;
	struct scope *fs;
	struct func *f;

	if (cpp_tok_kind() != CPP_TOPERATOR)
		error(&tok.loc, "expected 'operator'");
	next(); /* consume 'operator' */
	opcode = cpp_op_mangle(tok.kind);
	if (!opcode)
		error(&tok.loc, "unsupported operator for overloading");
	next(); /* consume the operator token */

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
	snprintf(mname, sizeof mname, "operator_%s", opcode);
	/* mkdecl/scopeputdecl keep the name pointer; persist it off the
	 * stack (token strings from the C parser are stable, ours are not). */
	pmangled = xmalloc(strlen(mname) + 1);
	strcpy(pmangled, mname);

	d = scopegetdecl(s, mname, false);
	if (d && d->kind != DECLFUNC)
		error(&tok.loc, "'%s' redeclared with different kind", mname);
	if (d && d->type && !typecompatible(ft, d->type))
		error(&tok.loc, "'%s' redeclared with incompatible type", mname);
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
		scopeputdecl(fs, pd);
	f = mkfunc(d, d->name, d->type, fs);
	stmt(f, fs);
	emitfunc(f, d->linkage == LINKEXTERN);
	delscope(fs);
	delfunc(f);
	d->defined = true;
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
 * that defines it (`*owner`).  Recurses into anonymous (base-class)
 * members. */
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
		} else if (!m->name) {
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
		if (!bt->u.structunion.tag || !cpp_has_ctor(bt, bt->u.structunion.tag)) {
			/* no user constructor: still construct its bases and any
			 * class-typed data members */
			if (has_base(bt))
				emit_base_ctors_for(f, bt, call_this);
			continue;
		}
		snprintf(mname, sizeof mname, "%s_%s", bt->u.structunion.tag, bt->u.structunion.tag);
		fd = scopegetdecl(bt->scope ? bt->scope : &filescope, mname, true);
		if (!fd || fd->kind != DECLFUNC)
			continue;
		fn = mkexpr(EXPRIDENT, fd->type, NULL);
		fn->u.ident.decl = fd;
		fn = decay(fn); /* &Base_Base */
		call = mkexpr(EXPRCALL, &typevoid, fn);
		call->u.call.args = call_this;
		call->u.call.nargs = 1;
		funcexpr(f, call);
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
	struct decl *fd;
	struct expr *fn, *obj, *call, *a, **end;
	size_t n = 0;

	if (!f || !d)
		return;
	tag = t ? t->u.structunion.tag : NULL;
	if (!tag || !cpp_has_ctor(t, tag)) {
		error(&tok.loc, "no matching constructor for object '%s'", d->name);
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
		error(&tok.loc, "no matching constructor for object '%s'", d->name);
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
	for (a = args; a; a = a->next) {
		*end = a;
		end = &a->next;
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
	/* C++ references mangle with a distinct 'R' marker so `f(Vec)` and
	 * `f(Vec &)` get different overload names (the caller binds an
	 * object by address, but the types must not collide). */
	if (t->isref && t->kind == TYPEPOINTER) {
		if (p + 1 <= end)
			*p++ = 'R';
		t = t->base;
	}
	switch (t->kind) {
	case TYPEVOID:     *p++ = 'v'; break;
	case TYPEBOOL:     *p++ = 'b'; break;
	case TYPECHAR:     *p++ = t->u.arith.issigned ? 'c' : 'C'; break;
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
		if (prefer_ref && args->lvalue && n + 1 < bufsz)
			buf[n++] = 'R';
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
 * `prefer_ref` marks lvalue arguments with the 'R' reference prefix,
 * matching cpp_mangled_name_args so a `f(Vec&)` overload is preferred
 * on lvalues while rvalues fall back to the by-value overload. */
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
		if (prefer_ref && args->lvalue && n + 1 < bufsz)
			buf[n++] = 'R';
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
	struct type *owner;          /* enclosing class (member templates) */
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
/* Parameter-pack element counts of templates being replayed (pushed by the
 * variadic instantiation, popped when the replay completes).  `sizeof...`
 * consults the innermost count. */
static int g_cpp_pack_stack[64];
static int g_cpp_pack_depth;

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
		error(&tok.loc, "template call nesting too deep");
	g_cpp_tmpl_stack[g_cpp_tmpl_depth++] = name;
	e = mkexpr(EXPRIDENT, mkpointertype(cpp_tmpl_dummy_callee()->type, QUALNONE),
	           NULL);
	e->u.ident.decl = cpp_tmpl_dummy_callee();
	e->lvalue = false;
	return e;
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
	tmpl->next = NULL;
	pe = &tmpl->params;

	/* template parameter list: `typename T` / `class T`, comma separated;
	 * a trailing parameter pack: `typename... Args` / `class... Args` */
	for (;;) {
		enum cpp_tokenkind k = cpp_tok_kind();
		if (k != CPP_TTYPENAME && k != CPP_TCLASS)
			error(&tok.loc, "expected 'typename' or 'class' in template parameter list");
		next(); /* consume typename/class */
		p = xmalloc(sizeof(*p));
		p->is_pack = false;
		if (tok.kind == TELLIPSIS) {
			p->is_pack = true;
			next();
		}
		if (tok.kind < TIDENT)
			error(&tok.loc, "expected template parameter name");
		p->name = xmalloc(strlen(tokenstr(tok.kind)) + 1);
		strcpy((char *)p->name, tokenstr(tok.kind));
		p->next = NULL;
		*pe = p;
		pe = &p->next;
		++tmpl->nparams;
		next();
		if (tok.kind == TGREATER)
			break;
		if (p->is_pack)
			error(&tok.loc, "template parameter pack must be the last parameter");
		expect(TCOMMA, "',' or '>' in template parameter list");
	}
	next(); /* consume '>' */

	/* class template: `template<...> class Foo { ... }` (struct/union too) */
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
		error(&tok.loc, "unable to determine template function name");

	*g_cpp_templates_end = tmpl;
	g_cpp_templates_end = &tmpl->next;
}

/* Deduce template arguments from the call-site argument list: parameter i
 * takes the type of argument i (positional; sufficient for the common
 * `T f(T a, T b)` / `T f(T a, U b)` forms).  A trailing parameter pack
 * collects every remaining argument type. */
static bool
cpp_tmpl_deduce(struct cpp_template *tmpl, struct expr *arglist,
                struct type **out, int *nout)
{
	struct cpp_tmpl_param *p;
	struct expr *a;
	int nfix = 0;
	int i = 0;

	for (p = tmpl->params; p && !p->is_pack; p = p->next)
		++nfix;
	for (a = arglist; a; a = a->next) {
		if (i < 16)
			out[i] = a->type;
		else
			error(&tok.loc, "too many arguments for template '%s'", tmpl->name);
		++i;
	}
	if (i < nfix)
		return false; /* too few arguments for the fixed parameters */
	if (i > 16)
		error(&tok.loc, "too many arguments for template '%s'", tmpl->name);
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
		error(&tok.loc, "template '%s' has too many parameters", name);
	if (!cpp_tmpl_deduce(tmpl, arglist, types, &nt))
		error(&tok.loc, "too few arguments for template '%s'", name);

	/* mangled name: name + "_" + type codes (e.g. max_i) */
	snprintf(key, sizeof key, "%s", name);
	for (i = 0; i < nt; ++i) {
		char code[64];
		cpp_mangle_type(types[i], code, sizeof code);
		strncat(key, "_", sizeof key - strlen(key) - 1);
		strncat(key, code, sizeof key - strlen(key) - 1);
	}
	snprintf(fnname, sizeof fnname, "%s", key);

	for (inst = tmpl->insts; inst; inst = inst->next)
		if (strcmp(inst->key, key) == 0)
			return inst->fn;

	/* instantiate: bind parameters as type names, replay the declaration.
	 * The buffered tokens are shared across instantiations, so rename on
	 * a private copy. */
	bs = mkscope(s);
	{
		int nfix = 0, npack = 0;
		/* fixed parameters bind positionally; a trailing parameter pack
		 * binds its first element as a placeholder (the per-element types
		 * are registered as __tp0..__tpN-1 for the pack expansion below) */
		for (p = tmpl->params, i = 0; p && !p->is_pack; p = p->next, ++i, ++nfix) {
			td = mkdecl((char *)p->name, DECLTYPE, types[i], QUALNONE, LINKNONE);
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
			error(&tok.loc, "cannot locate function name in template '%s'", name);

		cur = tok;
		tokpush(&cur, 1);
		tokpush(rtoks, ntoks);
		/* rtoks stays alive (tokpush stores pointers) until decl() consumes
		 * it below; deliberately not freed (bounded per-instantiation). */
	}
	next();
	if (!decl(bs, NULL))
		error(&tok.loc, "failed to instantiate template '%s'", name);
	if (g_cpp_pack_depth > 0)
		--g_cpp_pack_depth;

	fd = scopegetdecl(bs, fnname, 1);
	if (!fd || fd->kind != DECLFUNC)
		error(&tok.loc, "template '%s' instantiation produced no function", name);
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

/* Instantiate a class template: `Foo<...>` (tok positioned at '<').
 * Parses the explicit template arguments, replays the buffered
 * `class Foo { ... }` declaration (with each parameter bound and the tag
 * renamed to the mangled instantiation name) through cpp_class_decl, and
 * returns the instantiated class type. */
struct type *
cpp_tmpl_class_instantiate(struct scope *s, const char *name)
{
	extern struct type *typename(struct scope *, enum typequal *,
	    struct expr **);
	struct cpp_template *tmpl;
	struct cpp_tmpl_cls_inst *ci;
	struct cpp_tmpl_param *p;
	struct type *args[16];
	struct type *t;
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
		error(&tok.loc, "template '%s' has too many parameters", name);

	/* explicit template arguments `<T1, T2, ...>` */
	expect(TLESS, "after class template name");
	do {
		if (n >= tmpl->nparams)
			error(&tok.loc, "too many template arguments for class template '%s'", name);
		tq = QUALNONE;
		toeval = NULL;
		args[n++] = typename(s, &tq, &toeval);
		if (tok.kind == TGREATER)
			break;
		expect(TCOMMA, "',' or '>' in class template argument list");
	} while (tok.kind != TGREATER);
	next(); /* consume '>' */
	if (n < tmpl->nparams)
		error(&tok.loc, "too few template arguments for class template '%s'", name);

	/* mangled tag name: Foo + "_" + type codes (e.g. Foo_i) */
	snprintf(key, sizeof key, "%s", name);
	for (i = 0; i < tmpl->nparams; ++i) {
		char code[64];
		cpp_mangle_type(args[i], code, sizeof code);
		strncat(key, "_", sizeof key - strlen(key) - 1);
		strncat(key, code, sizeof key - strlen(key) - 1);
	}
	snprintf(tag, sizeof tag, "%s", key);

	for (ci = tmpl->cls_insts; ci; ci = ci->next)
		if (strcmp(ci->key, key) == 0)
			return ci->t;

	/* bind the parameters as type names (re-put replaces the previous
	 * binding; the names are generic template params and stay benignly in
	 * file scope) */
	for (p = tmpl->params, i = 0; p; p = p->next, ++i) {
		td = mkdecl((char *)p->name, DECLTYPE, args[i], QUALNONE, LINKNONE);
		scopeputdecl(&filescope, td);
	}
	/* rename the class-name token to the mangled tag, and every
	 * constructor/destructor token (which spells the original class name)
	 * so struct_decl recognizes them as the class's own constructors.
	 * The buffered tokens are shared across instantiations, so rename on
	 * a private copy. */
	{
		struct token *rtoks = xmalloc(tmpl->ntoks * sizeof *rtoks);
		bool found = false;
		memcpy(rtoks, tmpl->toks, tmpl->ntoks * sizeof *rtoks);
		for (i = 0; i < (int)tmpl->ntoks; ++i) {
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
			if (!is_param && strcmp(nm, name) == 0) {
				/* the class name itself or a constructor/destructor */
				rtoks[i].kind = tokenget(tag, strlen(tag));
				found = true;
			}
		}
		if (!found)
			error(&tok.loc, "cannot locate class name in template '%s'", name);

		/* replay `class Foo_i { ... }` to define the instantiated class.
		 * rtoks stays alive (tokpush stores pointers) until cpp_class_decl
		 * consumes it below; deliberately not freed (bounded). */
		cur = tok;
		tokpush(&cur, 1);
		tokpush(rtoks, tmpl->ntoks);
	}
	next();
	cpp_class_decl(&filescope);

	t = scopegettag(&filescope, tag, 1);
	if (!t)
		error(&tok.loc, "class template '%s' instantiation produced no class", name);

	ci = xmalloc(sizeof(*ci));
	snprintf(ci->key, sizeof ci->key, "%s", key);
	ci->t = t;
	ci->next = NULL;
	*tmpl->cls_insts_end = ci;
	tmpl->cls_insts_end = &ci->next;
	return t;
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
			error(&tok.loc, "too many explicit template arguments");
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
	    char **, int *, struct scope **, bool);
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
		error(&tok.loc, "member template of an unnamed class is not supported");

	for (tmpl = g_cpp_templates; tmpl; tmpl = tmpl->next)
		if (tmpl->is_member && tmpl->owner == owner &&
		    strcmp(tmpl->name, name) == 0)
			break;
	if (!tmpl)
		error(&tok.loc, "no template member function '%s' in class '%s'",
		    name, tag);
	if (tmpl->nparams > 16)
		error(&tok.loc, "template member '%s' has too many parameters",
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
		error(&tok.loc,
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
	 * the replayed `T get_i() {...}` defines `Wrapper_get_i`. */
	bs = mkscope(s);
	for (p = tmpl->params, i = 0; p; p = p->next, ++i) {
		td = mkdecl((char *)p->name, DECLTYPE, types[i], QUALNONE, LINKNONE);
		scopeputdecl(bs, td);
	}
	{
		struct token *rtoks = xmalloc(tmpl->ntoks * sizeof *rtoks);
		memcpy(rtoks, tmpl->toks, tmpl->ntoks * sizeof *rtoks);
		found = false;
		for (i = 0; i < (int)tmpl->ntoks; ++i) {
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
				rtoks[i].kind = tokenget(mname, strlen(mname));
				found = true;
				break;
			}
		}
		if (!found)
			error(&tok.loc, "cannot locate member name in template '%s'", name);

		cur = tok;
		tokpush(&cur, 1);
		tokpush(rtoks, tmpl->ntoks);
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
			error(&tok.loc, "no type in template member declaration");
		mt = declarator(bs, base, &dname, &align, NULL, false);
		if (mt.type->kind != TYPEFUNC)
			error(&tok.loc, "template member '%s' is not a function", name);
		cpp_define_method(bs, mt.type, mname, tag, false,
		    (sc & SCSTATIC) != 0, false);
		/* cpp_define_method appends the encoded explicit parameter types
		 * to the mangled symbol (`Wrapper_add_i` + `i` -> `Wrapper_add_ii`),
		 * so build the full symbol name the same way for the lookup. */
		{
			struct decl *cur;
			snprintf(sym, sizeof sym, "%s_%s", tag, mname);
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
		error(&tok.loc, "template member '%s' instantiation produced no function", name);

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
	struct decl *d;        /* the enclosing-scope variable decl */
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

/* Parse a C++11 lambda expression `[captures](params) -> ret { body }` and
 * lower it to an anonymous closure class (`__lambdaN`) whose `operator()`
 * is the lambda body and whose members are the by-value captures; returns
 * a freshly constructed closure object (an anonymous temporary).
 *
 * The closure class is defined by replaying a synthesized
 * `class __lambdaN { ... }` through cpp_class_decl, reusing the existing
 * member/constructor/operator machinery.  By-reference captures, default
 * captures (`[=]` / `[&]`), init-captures and generic (auto) parameters
 * are not supported yet. */
struct expr *
cpp_lambda_expr(struct scope *s)
{
	extern struct func *curfunc;
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);
	extern void funcinit(struct func *, struct decl *, struct init *,
	    bool);
	extern void tokpush(struct token *, size_t);

	struct cpp_lambda_cap caps[16];
	int ncap = 0;
	struct token *ptoks = NULL, *rtoks = NULL, *btoks = NULL;
	size_t pn = 0, pcap = 0, rn = 0, rcap = 0, bn = 0, bcap = 0;
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

	/* --- capture list `[ x, y ]` --- */
	next(); /* consume '[' */
	while (tok.kind != TRBRACK) {
		if (tok.kind == TBAND)
			error(&tok.loc, "reference capture '[&...]' is not supported yet (by-value capture only)");
		if (tok.kind < TIDENT)
			error(&tok.loc, "expected capture name in lambda capture list");
		if (ncap >= (int)countof(caps))
			error(&tok.loc, "too many captures in lambda");
		caps[ncap].name = tokenstr(tok.kind);
		caps[ncap].by_ref = false;
		caps[ncap].d = scopegetdecl(s, caps[ncap].name, 1);
		if (!caps[ncap].d || caps[ncap].d->kind != DECLOBJECT)
			error(&tok.loc, "cannot capture variable '%s'", caps[ncap].name);
		caps[ncap].t = caps[ncap].d->type;
		++ncap;
		next();
		if (tok.kind == TRBRACK)
			break;
		expect(TCOMMA, "',' or ']' in lambda capture list");
	}
	next(); /* consume ']' */

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
			error(&tok.loc, "expected lambda body");
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
			 * the closure member declaration `__lti cap_i` is typed. */
			snprintf(tn, sizeof tn, "__lt%d", i);
			td = mkdecl(xmalloc(strlen(tn) + 1), DECLTYPE, caps[i].t,
			    QUALNONE, LINKNONE);
			strcpy((char *)td->name, tn);
			scopeputdecl(&filescope, td);
		}
	}
	wcap = 64 + (size_t)ncap * 24 + pn + rn + bn + 8;
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
	/* synthesized constructor `__lambdaN(__lt0 __c0, ...) { cap = __c; }` */
	cpp_tb(wtoks, &wn, tmpl, 0, tagname);
	cpp_tb(wtoks, &wn, tmpl, TLPAREN, NULL);
	for (i = 0; i < ncap; ++i) {
		if (i)
			cpp_tb(wtoks, &wn, tmpl, TCOMMA, NULL);
		snprintf(tn, sizeof tn, "__lt%d", i);
		cpp_tb(wtoks, &wn, tmpl, 0, tn);
		snprintf(cn, sizeof cn, "__c%d", i);
		cpp_tb(wtoks, &wn, tmpl, 0, cn);
	}
	cpp_tb(wtoks, &wn, tmpl, TRPAREN, NULL);
	cpp_tb(wtoks, &wn, tmpl, TLBRACE, NULL);
	for (i = 0; i < ncap; ++i) {
		cpp_tb(wtoks, &wn, tmpl, 0, caps[i].name);
		cpp_tb(wtoks, &wn, tmpl, TASSIGN, NULL);
		snprintf(cn, sizeof cn, "__c%d", i);
		cpp_tb(wtoks, &wn, tmpl, 0, cn);
		cpp_tb(wtoks, &wn, tmpl, TSEMICOLON, NULL);
	}
	cpp_tb(wtoks, &wn, tmpl, TRBRACE, NULL);
	/* `operator()(params) { body }` — `ret` is the explicit `->` type if
	 * given, otherwise `auto` (deduced from the body's return). */
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
		memcpy(wtoks + wn, ptoks, pn * sizeof *ptoks);
		wn += pn;
	}
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
		error(&tok.loc, "lambda closure class '%s' was not created", tagname);

	/* --- construct the closure object (anonymous temporary) --- */
	if (!curfunc)
		error(&tok.loc, "lambda used outside of a function body is not supported");
	tmp = mkdecl("tmp", DECLOBJECT, ct, QUALNONE, LINKNONE);
	tmp->u.obj.storage = SDAUTO;
	funcinit(curfunc, tmp, NULL, false); /* allocate storage */
	args = NULL;
	ae = &args;
	for (i = 0; i < ncap; ++i) {
		struct expr *cap = mkexpr(EXPRIDENT, caps[i].t, NULL);
		cap->qual = caps[i].d->qual;
		cap->lvalue = true;
		cap->u.ident.decl = caps[i].d;
		*ae = cap;
		ae = &cap->next;
	}
	cpp_emit_ctor_call(curfunc, tmp, args);

	e = mkexpr(EXPRIDENT, ct, NULL);
	e->lvalue = true;
	e->u.ident.decl = tmp;
	return e;
}

/* --- C++ constexpr functions (compile-time evaluation, phase 1/2) ------- */

/* A constexpr function whose body is buffered so a constant-context call
 * (`constexpr int v = sq(5);`, static_assert) can be folded by replaying
 * `{ return <expr> ; }` with the argument values bound. */
struct cpp_cexpr_fn {
	struct decl *fd;         /* the constexpr function decl */
	char **params;           /* parameter names */
	struct type **ptypes;    /* parameter types */
	int nparams;
	struct token *toks;      /* `{ return <expr> ; }` body tokens */
	size_t ntoks;
	struct cpp_cexpr_fn *next;
};

static struct cpp_cexpr_fn *g_cpp_cexpr_fns;
static int g_cpp_cexpr_depth;   /* recursion limit */

/* Buffer a constexpr function's `{ ... }` body (called from decl() with
 * tok positioned on '{'), then replay it so the normal runtime definition
 * is still emitted by the caller's stmt(). */
void
cpp_buffer_constexpr_body(struct decl *d)
{
	extern void tokpush(struct token *, size_t);

	struct cpp_cexpr_fn *fn;
	struct decl *pd;
	size_t cap = 0, ntok = 0;
	int bd = 0, n = 0;

	fn = xmalloc(sizeof *fn);
	fn->fd = d;
	fn->nparams = 0;
	for (pd = d->type->u.func.params; pd; pd = pd->next)
		++fn->nparams;
	fn->params = xmalloc(fn->nparams * sizeof *fn->params);
	fn->ptypes = xmalloc(fn->nparams * sizeof *fn->ptypes);
	for (pd = d->type->u.func.params; pd; pd = pd->next) {
		fn->params[n] = pd->name ? pd->name : (char *)"";
		fn->ptypes[n] = pd->type;
		++n;
	}
	fn->toks = NULL;
	fn->ntoks = 0;
	while (tok.kind != TEOF) {
		if (ntok >= cap) {
			cap = cap ? cap * 2 : 32;
			fn->toks = xreallocarray(fn->toks, cap, sizeof *fn->toks);
		}
		/* copy the token; the scanner reuses its literal buffer on the
		 * next next(), so keep a private copy of `lit` for the replay */
		{
			struct token tt = tok;
			if (tt.lit)
				tt.lit = strdup(tt.lit);
			fn->toks[ntok++] = tt;
		}
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
	fn->ntoks = ntok;
	/* Replay the body in front of the token that follows it so the
	 * caller's stmt() parses the runtime definition and the parse then
	 * continues at the right stream position.  The trailing token must
	 * outlive this function (stmt() consumes it only after we return),
	 * so it is copied to the heap rather than pushed as a stack local. */
	{
		struct token *trail = xmalloc(sizeof *trail);
		*trail = tok;
		if (trail->lit)
			trail->lit = strdup(trail->lit);
		tokpush(trail, 1);
		tokpush(fn->toks, fn->ntoks);
		next(); /* position tok at '{' */
	}
	fn->next = g_cpp_cexpr_fns;
	g_cpp_cexpr_fns = fn;
}

/* Fold a constexpr function call when the callee is constexpr and every
 * argument is an integer constant; returns a fresh EXPRCONST or NULL. */
struct expr *
cpp_constexpr_eval(struct expr *call)
{
	extern struct scope filescope;
	extern struct func *curfunc;
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);
	extern void tokpush(struct token *, size_t);
	extern struct expr *expr(struct scope *);
	extern struct scope *mkscope(struct scope *);
	extern struct scope *delscope(struct scope *);
	extern void scopeputdecl(struct scope *, struct decl *);

	struct expr *callee = call ? call->base : NULL;
	struct decl *fd = NULL;
	struct cpp_cexpr_fn *fn;
	struct expr *a, *e, *r;
	struct scope *tmp;
	struct token cur;
	unsigned long long args[16];
	int i, nargs = 0;

	if (call->kind != EXPRCALL || !callee)
		return NULL;
	if (callee->kind == EXPRUNARY && callee->op == TBAND &&
	    callee->base && callee->base->kind == EXPRIDENT)
		fd = callee->base->u.ident.decl;
	else if (callee->kind == EXPRIDENT)
		fd = callee->u.ident.decl;
	if (!fd || fd->kind != DECLFUNC || !fd->u.func.isconstexpr)
		return NULL;
	for (fn = g_cpp_cexpr_fns; fn; fn = fn->next)
		if (fn->fd == fd)
			break;
	if (!fn)
		return NULL;

	for (a = call->u.call.args; a; a = a->next, ++nargs) {
		struct expr *ae = eval(a);
		if (nargs >= 16 || ae->kind != EXPRCONST ||
		    !(ae->type->prop & PROPINT))
			return NULL;
		args[nargs] = ae->u.constant.u;
	}
	if (nargs != fn->nparams)
		return NULL;
	if (g_cpp_cexpr_depth >= 64)
		error(&tok.loc, "constexpr evaluation recursion too deep");

	/* bind the parameters as integer constants and fold the body's
	 * return expression */
	tmp = mkscope(&filescope);
	for (i = 0; i < fn->nparams; ++i) {
		struct decl *pd = mkdecl(fn->params[i], DECLCONST,
		    fn->ptypes[i] ? fn->ptypes[i] : &typeint, QUALCONST,
		    LINKNONE);
		pd->u.enumconst = args[i];
		scopeputdecl(tmp, pd);
	}
	++g_cpp_cexpr_depth;
	{
		struct func *saved = curfunc;
		cur = tok;
		tokpush(&cur, 1);
		tokpush(fn->toks, fn->ntoks);
		next(); /* { */
		next(); /* return */
		next(); /* start of the return expression */
		e = expr(tmp);
		expect(TSEMICOLON, "after constexpr return expression");
		next(); /* } — back to cur */
		curfunc = saved;
	}
	--g_cpp_cexpr_depth;

	r = eval(e);
	/* eval() folds in place and returns `e` itself, so capture the
	 * constant value before the tree is freed below. */
	if (r->kind == EXPRCONST && (r->type->prop & PROPINT)) {
		struct expr *res = xmalloc(sizeof *res);
		*res = *r;
		delexpr(e);
		delscope(tmp);
		return res;
	}
	delexpr(e);
	delscope(tmp);
	return NULL;
}
