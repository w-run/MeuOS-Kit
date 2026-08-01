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
void cpp_define_method(struct scope *s, struct type *funct,
                              const char *mname, const char *class_tag);

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

static void cpp_emit_base_ctor(struct func *f);
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
	m = typemember(t, name, &offset);
	if (!m)
		return NULL;
	if (m->type && m->type->kind == TYPEFUNC) {
		/* member-function call: mangled free function + this */
		if (!cpp_is_member_function(t, name))
			return NULL;
		cpp_mangled_name(t, name, mname, sizeof mname);
		fd = scopegetdecl(t->scope ? t->scope : s, mname, 1);
		if (!fd || fd->kind != DECLFUNC)
			return NULL;
		e = mkexpr(EXPRIDENT, fd->type, NULL);
		e->qual = fd->qual;
		e->u.ident.decl = fd;
		e = decay(e); /* &Class_method */
		g_cpp_member_this = cpp_this_expr();
		return e;
	}
	/* data member: (*this).name — mirror postfixexpr()'s TPERIOD
	 * lowering of `obj.member`. */
	thise = cpp_this_expr();
	if (!thise)
		return NULL;
	base = mkbinaryexpr(&tok.loc, TADD, exprconvert(thise, &typeulong),
	                    mkconstexpr(&typeulong, offset));
	base->type = mkpointertype(m->type, m->qual);
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
 * which the C parser does not understand.  Currently handles the
 * data-member subset; member functions, inheritance, and templates are
 * added later. */
static bool
cpp_class_decl(struct scope *s)
{
	struct type *t, *base = NULL;
	char *tag;
	struct structbuilder b;
	bool is_class;

	/* class defaults to private access, struct/union to public (C++). */
	is_class = cpp_tok_kind() == CPP_TCLASS;

	/* class/struct/union keyword consumed here */
	next();
	if (tok.kind < TIDENT)
		error(&tok.loc, "expected class name");
	tag = tokenstr(tok.kind);
	next();

	/* base-class list: `class Derived : public Base {` (single
	 * inheritance for now; virtual bases and multiple bases later). */
	if (tok.kind == TCOLON) {
		next(); /* consume ':' */
		/* optional access specifier: public/private/protected */
		enum cpp_tokenkind bk = cpp_tok_kind();
		if (bk == CPP_TPUBLIC || bk == CPP_TPRIVATE || bk == CPP_TPROTECTED)
			next();
		if (tok.kind < TIDENT)
			error(&tok.loc, "expected base class name after ':'");
		base = scopegettag(s, tokenstr(tok.kind), true);
		if (!base || (base->kind != TYPESTRUCT && base->kind != TYPEUNION))
			error(&tok.loc, "'%s' is not a class type", tokenstr(tok.kind));
		next();
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
	if (base && base->incomplete)
		error(&tok.loc, "base class '%s' has incomplete type", base->u.structunion.tag);
	t->scope = s; /* member symbols are registered here */
	next(); /* consume '{' */

	b.type = t;
	b.last = &t->u.structunion.members;
	b.bits = 0;
	b.pack = false;
	b.access = is_class ? ACC_PRIVATE : ACC_PUBLIC;

	/* The base-class subobject occupies offset 0 of the derived class:
	 * register it as an anonymous member so typemember()'s recursive
	 * search and the layout computation see it like any other member. */
	if (base) {
		struct qualtype bq;
		bq.type = base;
		bq.qual = QUALNONE;
		bq.expr = NULL;
		addmember(&b, bq, NULL, 0, -1);
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
		if (tok.kind == TRBRACE)
			break;
		structdecl(s, &b);
	}
	next(); /* consume '}' */

	/* Finalize: align the aggregate size up to its member alignment and
	 * mark it complete, mirroring tagspec()'s struct branch. */
	if (t->align < 0)
		t->align = 0;
	if (t->size)
		t->size = ALIGNUP(t->size, t->align);
	t->incomplete = false;

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

		if (!decl(&filescope, NULL)) {
			if (tok.kind == TSEMICOLON)
				error(&tok.loc, "unexpected ';' at top-level");
			error(&tok.loc, "expected declaration or function definition");
		}
	}
	emittentativedefns();
}

/* --- member function lowering (C.2.3) -------------------------------- */

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
                  const char *class_tag)
{
	extern struct decl *mkdecl(char *, enum declkind, struct type *,
	    enum typequal, enum linkage);
	extern struct func *mkfunc(struct decl *, char *, struct type *,
	    struct scope *);
	extern void delfunc(struct func *);
	extern void stmt(struct func *, struct scope *);
	extern void emitfunc(struct func *, bool);
	extern void funchlt(struct func *);
	extern struct scope *delscope(struct scope *);

	char mangled[256];
	char *pmangled;
	struct type *mtype, *classt;
	struct decl *d, *thisd, *cur, *nd, **end;
	struct scope *fs;
	struct func *f;

	if (!class_tag || !mname)
		return;

	classt = scopegettag(s, class_tag, true);
	if (!classt || (classt->kind != TYPESTRUCT && classt->kind != TYPEUNION))
		error(&tok.loc, "'%s' is not a class type", class_tag);

	snprintf(mangled, sizeof mangled, "%s_%s", class_tag, mname);
	/* mkdecl/scopeputdecl keep the name pointer; persist it off the
	 * stack (the C parser's token strings are stable, ours is not). */
	pmangled = xmalloc(strlen(mangled) + 1);
	strcpy(pmangled, mangled);

	/* Build the mangled function type:
	 * `Class_method(Class *this, args...) -> funct->base`.  The
	 * declarator already parsed the explicit params into funct; we
	 * copy those decls so funct (kept in the member list for call
	 * lowering) and mtype don't share the same decl chain. */
	mtype = mktype(TYPEFUNC, 0);
	mtype->base = funct->base;
	mtype->qual = funct->qual;
	mtype->prop |= funct->prop;
	mtype->align = funct->align;
	mtype->u.func.isvararg = funct->u.func.isvararg;
	mtype->u.func.params = NULL;
	mtype->u.func.nparam = 0;
	thisd = mkdecl("this", DECLOBJECT, mkpointertype(classt, QUALNONE),
	               QUALNONE, LINKNONE);
	thisd->u.obj.storage = SDAUTO;
	mtype->u.func.params = thisd;
	end = &thisd->next;
	++mtype->u.func.nparam;
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

	/* Function definition: open a fresh scope seeded with this + the
	 * copied params, exactly as declarator's func branch + decl()'s
	 * DECLFUNC path set up funcscope. */
	fs = mkscope(s);
	for (nd = mtype->u.func.params; nd; nd = nd->next)
		scopeputdecl(fs, nd);

	g_cpp_method.class_type = classt;
	g_cpp_method.this_decl = thisd;
	g_cpp_method.active = true;

	f = mkfunc(d, d->name, d->type, fs);
	/* constructor: run base-class constructors before the body */
	if (strcmp(mname, class_tag) == 0)
		cpp_emit_base_ctor(f);
	stmt(f, fs);
	if (d->u.func.isnoreturn)
		funchlt(f);
	emitfunc(f, d->linkage == LINKEXTERN);
	delscope(fs);
	delfunc(f);
	d->defined = true;

	g_cpp_method.active = false;
	g_cpp_method.this_decl = NULL;
	g_cpp_method.class_type = NULL;
}

/* C++ member-function lookup helpers.  A function member is registered in
 * the struct/union member list by addmember (C++ mode); these helpers let
 * the postfix-expression lowering detect and mangle member calls.  The
 * lookup recurses through anonymous members, so inherited members (the
 * base-class subobject is an anonymous member at offset 0) resolve to
 * their defining class. */

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
	if (!cpp_has_ctor(t, tag))
		return false;
	snprintf(mname, sizeof mname, "%s_%s", tag, tag);
	fd = scopegetdecl(t->scope ? t->scope : &filescope, mname, true);
	if (!fd || fd->kind != DECLFUNC)
		return false;

	fn = mkexpr(EXPRIDENT, fd->type, NULL);
	fn->u.ident.decl = fd;
	fn = decay(fn); /* &Class_Class */

	obj = mkexpr(EXPRIDENT, d->type, NULL);
	obj->qual = d->qual;
	obj->lvalue = true;
	obj->u.ident.decl = d;
	obj = mkunaryexpr(TBAND, obj); /* &obj */

	call = mkexpr(EXPRCALL, &typevoid, fn);
	call->u.call.args = obj;
	call->u.call.nargs = 1;
	funcexpr(f, call);
	return true;
}

/* Emit implicit base-class construction at the start of a derived-class
 * constructor: `class D : B { D() {...} }` first runs `B_B(&this)` for
 * each direct base with a user default constructor (base subobject lives
 * at offset 0, so `this` already points at it). */
static void
cpp_emit_base_ctor(struct func *f)
{
	extern struct value *funcexpr(struct func *, struct expr *);
	extern struct scope filescope;

	struct member *m;
	struct type *bt;
	char mname[256];
	struct decl *fd;
	struct expr *fn, *call, *thisp;

	for (m = g_cpp_method.class_type->u.structunion.members; m; m = m->next) {
		if (m->name)
			continue; /* only anonymous members are base subobjects */
		bt = m->type;
		if (!bt || !bt->u.structunion.tag || !cpp_has_ctor(bt, bt->u.structunion.tag))
			continue;
		snprintf(mname, sizeof mname, "%s_%s", bt->u.structunion.tag, bt->u.structunion.tag);
		fd = scopegetdecl(bt->scope ? bt->scope : &filescope, mname, true);
		if (!fd || fd->kind != DECLFUNC)
			continue;
		fn = mkexpr(EXPRIDENT, fd->type, NULL);
		fn->u.ident.decl = fd;
		fn = decay(fn); /* &Base_Base */
		thisp = cpp_this_expr();
		call = mkexpr(EXPRCALL, &typevoid, fn);
		call->u.call.args = thisp;
		call->u.call.nargs = 1;
		funcexpr(f, call);
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
