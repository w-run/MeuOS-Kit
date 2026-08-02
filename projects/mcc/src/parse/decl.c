/* parse/decl.c -- top-level decl() and declaration wiring.
 *
 * Owns the entry points: decl(), declcommon(), defineobj(), mkdecl(),
 * stringdecl(), emittentativedefns(). The smaller grammar helpers
 * (storageclass/typequal/funcspec/tagspec/declspecs) live in specs.c;
 * the declarator grammar lives in declarator.c; struct/union member
 * grammar lives in struct_decl.c.
 *
 * All four files share struct qualtype, the SC/SPEC/FUNC prefix enums,
 * struct structbuilder, and the tentative-defns list through
 * decl_internal.h. The forward decls below resolve the cross-file
 * dependencies between decl.c and specs.c/declarator.c. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "mcc.h"
#include "decl_internal.h"
#include "cpp.h"
struct decl *tentativedefns, **tentativedefnsend = &tentativedefns;

/* C++ free-function overloading: two functions differ only in their
 * parameter lists — never in their return type.  Compare only the
 * parameter signatures (arity, varargs, and each parameter type). */
static bool
same_func_params(struct type *a, struct type *b)
{
	struct decl *pa, *pb;

	if (!a || !b || a->kind != TYPEFUNC || b->kind != TYPEFUNC)
		return false;
	if (a->u.func.nparam != b->u.func.nparam ||
	    a->u.func.isvararg != b->u.func.isvararg)
		return false;
	for (pa = a->u.func.params, pb = b->u.func.params;
	     pa && pb; pa = pa->next, pb = pb->next) {
		if (!typecompatible(pa->type, pb->type))
			return false;
	}
	return true;
}

struct decl *
mkdecl(char *name, enum declkind k, struct type *t, enum typequal tq, enum linkage linkage)
{
	struct decl *d;

	d = xmalloc(sizeof(*d));
	memset(d, 0, sizeof(*d));
	d->name = name;
	d->kind = k;
	d->linkage = linkage;
	d->type = t;
	d->qual = tq;
	if (k == DECLOBJECT && t)
		d->u.obj.align = t->align;

	return d;
}
static enum linkage
getlinkage(enum declkind kind, enum storageclass sc, struct decl *prior, bool filescope)
{
	if (sc & SCSTATIC)
		return filescope ? LINKINTERN : LINKNONE;
	if (sc & SCEXTERN || kind == DECLFUNC)
		return prior ? prior->linkage : LINKEXTERN;
	return filescope ? LINKEXTERN : LINKNONE;
}
static struct decl *
declcommon(struct scope *s, enum declkind kind, char *name, char *asmname, struct type *t, enum typequal tq, enum storageclass sc, struct decl *prior)
{
	struct decl *d;
	enum linkage linkage;
	const char *kindstr = kind == DECLFUNC ? "function" : "object";
	/* A C++ namespace scope is a file-level context for linkage: a
	 * namespace-global `int x;` gets external linkage like a file-scope
	 * one (otherwise it would be treated as auto and funcinit(NULL)
	 * would crash on the tentative definition). */
	bool fscope = s == &filescope || s->name != NULL;

	if (prior) {
		if (prior->linkage == LINKNONE)
			error(&tok.loc, "%s '%s' with no linkage redeclared", kindstr, name);
		linkage = getlinkage(kind, sc, prior, fscope);
		if (prior->linkage != linkage)
			error(&tok.loc, "%s '%s' redeclared with different linkage", kindstr, name);
		if (!typecompatible(t, prior->type) || tq != prior->qual)
			error(&tok.loc, "%s '%s' redeclared with incompatible type", kindstr, name);
		if (asmname && (!prior->asmname || strcmp(prior->asmname, asmname) != 0))
			error(&tok.loc, "%s '%s' redeclared with different assembler name", kindstr, name);
		prior->type = typecomposite(t, prior->type);
		return prior;
	}
	if (s->parent)
		prior = scopegetdecl(s->parent, name, true);
	linkage = getlinkage(kind, sc, prior, fscope);
	if (linkage != LINKNONE && s->parent) {
		/* XXX: should maintain map of identifiers with linkage to their declaration, and use that */
		if (s->parent != &filescope)
			prior = scopegetdecl(&filescope, name, false);
		if (prior && prior->linkage != LINKNONE) {
			if (prior->kind != kind)
				error(&tok.loc, "'%s' redeclared with different kind", name);
			if (prior->linkage != linkage)
				error(&tok.loc, "%s '%s' redeclared with different linkage", kindstr, name);
			if (!typecompatible(t, prior->type) || tq != prior->qual)
				error(&tok.loc, "%s '%s' redeclared with incompatible type", kindstr, name);
			if (!asmname)
				asmname = prior->asmname;
			else if (!prior->asmname || strcmp(prior->asmname, asmname) != 0)
				error(&tok.loc, "%s '%s' redeclared with different assembler name", kindstr, name);
			t = typecomposite(t, prior->type);
		}
	}
	d = mkdecl(name, kind, t, tq, linkage);
	d->asmname = asmname;
	scopeputdecl(s, d);
	return d;
}
static void
defineobj(struct decl *d, struct init *init, bool hasinit, struct func *f)
{
	if (d->type->incomplete)
		error(&tok.loc, "object '%s' has incomplete type", d->name);
	if (d->u.obj.align < d->type->align)
		d->u.obj.align = d->type->align;
	/* C23/C++ constexpr variable: must have a constant expression
	 * initializer; the C++ phase-1 subset additionally requires an
	 * integer constant (`constexpr int x = 1.5;` is rejected). */
	if ((d->qual & QUALCONSTEXPR) && hasinit && init->expr) {
		extern int g_lang;
		struct expr *e = eval(init->expr);
		if (e->kind != EXPRCONST)
			error(&tok.loc, "constexpr variable '%s' requires a constant expression initializer", d->name);
		if (g_lang == 1 && !(e->type->prop & PROPINT))
			error(&tok.loc, "constexpr variable '%s' requires a constant integer expression initializer", d->name);
		/* remember the value so a later constant expression can reuse it */
		d->u.obj.constval = e->u.constant.u;
		d->u.obj.has_constval = true;
		init->expr = e;
	}
	if (d->u.obj.storage == SDAUTO)
		funcinit(f, d, init, hasinit);
	else
		emitdata(d, init);
	d->defined = true;
}
bool
decl(struct scope *s, struct func *f)
{
	struct qualtype base, qt;
	struct type *t;
	enum typequal tq;
	enum storageclass sc;
	enum funcspec fs;
	struct attr a;
	struct init *init;
	bool hasinit;
	char *name, *asmname;
	int allowfunc = !f;
	struct decl *d, *prior;
	enum declkind kind;
	struct scope *funcscope;
	int align;

	if (staticassert(s))
		return true;
	a.kind = 0;
	if (attr(&a, ATTRNORETURN | ATTRFALLTHROUGH | ATTRNODISCARD | ATTRMAYBEUNUSED | ATTRDEPRECATED) && consume(TSEMICOLON))
		return true;
	base = declspecs(s, &sc, &fs, &align);
	if (!base.type)
		return false;
	/* C++ non-member operator overload: `Vec operator+(Vec a, Vec b)`.
	 * The return type is already parsed; `operator` follows. */
	{
		extern int g_lang;
		extern enum cpp_tokenkind cpp_tok_kind(void);
		extern void cpp_parse_free_operator(struct scope *,
		    struct qualtype);
		if (g_lang == 1 && cpp_tok_kind() == CPP_TOPERATOR) {
			cpp_parse_free_operator(s, base);
			return true;
		}
	}
	if (f) {
		if (sc == SCTHREADLOCAL)
			error(&tok.loc, "block scope declaration containing 'thread_local' must contain 'static' or 'extern'");
	} else {
		/* 6.9p2 */
		if (sc & SCAUTO)
			error(&tok.loc, "external declaration must not contain 'auto'");
		if (sc & SCREGISTER)
			error(&tok.loc, "external declaration must not contain 'register'");
	}
	if (consume(TSEMICOLON)) {
		/* XXX 6.7p2 error unless in function parameter/struct/union, or tag/enum members are declared */
		return true;
	}
	for (;;) {
		/* C++ constructor-call declarator: `Point p(3, 4);` */
		bool ctor_call = false;
		struct expr *ctor_args = NULL;

		qt = declarator(s, base, &name, NULL, &funcscope, false);
		t = qt.type;
		tq = qt.qual;
		if (qt.expr == (struct expr *)1) {
			/* declarator consumed `(args)`; the object type is the
			 * base (class) type, and the args are in the cpp module */
			extern int g_lang;
			extern struct expr *cpp_ctor_args_take(void);
			if (g_lang == 1) {
				ctor_call = true;
				ctor_args = cpp_ctor_args_take();
				t = base.type;
				tq = base.qual;
			}
		}
		if (consume(T__ASM__)) {
			struct stringlit lit;

			expect(TLPAREN, "after __asm__");
			tokencheck(&tok, TSTRINGLIT, "after __asm__");
			stringconcat(&lit, true);
			asmname = lit.data;
			expect(TRPAREN, "after assembler name");
			allowfunc = 0;
		} else {
			asmname = NULL;
		}
		gnuattr(&a, (enum attrkind)(ATTRWEAK | ATTRUSED | ATTRNOINLINE | ATTRALWAYSINLINE |
		    ATTRCONSTRUCTOR | ATTRDESTRUCTOR | ATTRSECTION | ATTRALIGNED |
		    ATTRNORETURN | ATTRDEPRECATED));  /* appertains to identifier */
		kind = sc & SCTYPEDEF ? DECLTYPE : t->kind == TYPEFUNC ? DECLFUNC : DECLOBJECT;
		prior = scopegetdecl(s, name, false);
		if (prior && prior->kind != kind)
			error(&tok.loc, "'%s' redeclared with different kind", name);
		/* C++ `Class::name` qualified declarator: consumed by the
		 * DECLFUNC path (out-of-line method definition); anything else
		 * (static data members, typedefs) is not supported yet.  Only
		 * take the qualifier here for non-function kinds — DECLFUNC
		 * consumes it itself. */
		{
			extern int g_lang;
			extern const char *cpp_take_qual_class(void);
			if (g_lang == 1 && kind != DECLFUNC) {
				const char *qclass = cpp_take_qual_class();
				/* C++ static data member definition:
				 * `int Class::count = 0;` */
				if (qclass && kind == DECLOBJECT) {
					extern void cpp_define_static_data(struct scope *,
					    const char *, const char *);
					cpp_define_static_data(s, qclass, name);
					return true;
				}
				if (qclass)
					error(&tok.loc, "qualified name in non-function declaration is not supported yet");
			}
		}
		switch (kind) {
		case DECLTYPE:
			if (align)
				error(&tok.loc, "typedef '%s' declared with alignment specifier", name);
			if (asmname)
				error(&tok.loc, "typedef '%s' declared with assembler label", name);
			if (!prior)
				scopeputdecl(s, mkdecl(name, DECLTYPE, t, tq, LINKNONE));
			else if (!typesame(prior->type, t) || prior->qual != tq)
				error(&tok.loc, "typedef '%s' redefined with different type", name);
			break;
		case DECLOBJECT:
			if (align && align < t->align)
				error(&tok.loc, "object '%s' requires alignment %d, which is stricter than specified alignment %d", name, t->align, align);
			d = declcommon(s, kind, name, asmname, t, tq, sc, prior);
			if (d->u.obj.align < align)
				d->u.obj.align = align;
			/* C++11 `auto x = expr;`: the placeholder type (&typeauto) is
			 * deduced from the initializer before the storage/type
			 * machinery (mkglobal, parseinit) runs. */
			{
				extern int g_lang;
				if (g_lang == 1 && d->type == &typeauto) {
					struct expr *auto_expr;
					if (ctor_call)
						error(&tok.loc, "'auto' cannot be initialized with a constructor call");
					if (!consume(TASSIGN))
						error(&tok.loc, "'auto' variable requires an initializer");
					auto_expr = assignexpr(s);
					if (!auto_expr->type || auto_expr->type == &typeauto ||
					    auto_expr->type->kind == TYPEVOID)
						error(&tok.loc,
						    "unable to deduce the type of 'auto' variable '%s'",
						    name);
					t = auto_expr->type;
					d->type = t;
					d->qual = tq;
					init = mkinit(0, t->size, (struct bitfield){0},
					    auto_expr);
					hasinit = true;
				} else {
					init = NULL;
					hasinit = false;
				}
			}
			if (d->linkage == LINKNONE && !(sc & SCSTATIC)) {
				d->u.obj.storage = SDAUTO;
			} else {
				d->u.obj.storage = sc & SCTHREADLOCAL ? SDTHREAD : SDSTATIC;
				if (t->prop & PROPVM)
					error(&tok.loc, "object '%s' with %s storage duration cannot have variably modified type", name, d->u.obj.storage == SDSTATIC ? "static" : "thread");
				d->value = mkglobal(d);
			}

			if (base.expr)
				funcexpr(f, base.expr);
			if (consume(TASSIGN)) {
				if (f && d->linkage != LINKNONE)
					error(&tok.loc, "object '%s' with block scope and %s linkage cannot have initializer", name, d->linkage == LINKEXTERN ? "external" : "internal");
				if (d->defined)
					error(&tok.loc, "object '%s' redefined", name);
				init = parseinit(s, d->type);
				hasinit = true;
			} else if (!hasinit && sc & SCEXTERN) {
				break;
			} else if (!hasinit && d->linkage != LINKNONE && d->u.obj.storage == SDSTATIC) {
				if (!d->defined && !d->tentative) {
					/* C++ global class object: defer construction to
					 * __mxx_global_var_init (runs before main). */
					extern int g_lang;
					extern void cpp_record_global_ctor(struct decl *,
					    struct expr *);
					if (g_lang == 1)
						cpp_record_global_ctor(d,
						    ctor_call ? ctor_args : NULL);
					d->tentative = true;
					*tentativedefnsend = d;
					tentativedefnsend = &d->next;
				}
				break;
			}
			defineobj(d, init, hasinit, f);
			/* C++: a class-typed local with a user constructor gets a
			 * construction call after its storage is laid out:
			 * `Counter c;` -> `Counter_Class(&c)`, or with arguments
			 * `Point p(3, 4);` -> `Point_Point(&p, 3, 4)`. */
			{
				extern int g_lang;
				extern bool cpp_emit_default_ctor(struct func *,
				    struct decl *);
				extern void cpp_emit_ctor_call(struct func *,
				    struct decl *, struct expr *);
				extern void cpp_record_global_ctor(struct decl *,
				    struct expr *);
				if (g_lang == 1) {
					if (ctor_call && !f && d->u.obj.storage == SDSTATIC)
						/* global object with ctor args: defer to
						 * __mxx_global_var_init */
						cpp_record_global_ctor(d, ctor_args);
					else if (ctor_call)
						cpp_emit_ctor_call(f, d, ctor_args);
					else if (d->u.obj.storage == SDSTATIC &&
					    d->defined && !f)
						/* global object with a ctor: defer the
						 * construction call to __mxx_global_var_init */
						cpp_record_global_ctor(d, NULL);
					else
						cpp_emit_default_ctor(f, d);
					/* record local class objects for reverse-order
					 * destruction at scope exit (head-insert: newest
					 * first, so front-to-back is reverse declaration) */
					if (d->u.obj.storage == SDAUTO &&
					    (t->kind == TYPESTRUCT || t->kind == TYPEUNION)) {
						d->next = s->objects;
						s->objects = d;
					}
				}
			}
			break;
		case DECLFUNC:
			if (align)
				error(&tok.loc, "function '%s' declared with alignment specifier", name);
			if (f && sc && sc != SCEXTERN)  /* 6.7.1p7 */
				error(&tok.loc, "function '%s' with block scope may only have storage class 'extern'", name);
			/* C++ out-of-line member-function definition:
			 * `void Class::method(...) { ... }`.  The declarator mangled
			 * the name to `Class_method`; route through cpp_define_method
			 * so the implicit `this` parameter and in-body member access
			 * work exactly as for in-class definitions. */
			{
				extern const char *cpp_take_qual_class(void);
				extern void cpp_define_method(struct scope *,
				    struct type *, const char *, const char *, bool, bool);
				const char *qclass = cpp_take_qual_class();
				if (qclass) {
					const char *mname = name + strlen(qclass) + 1;
					bool mc = false;
					if (tok.kind == TCONST) {
						mc = true;
						next();
					}
					cpp_define_method(s, t, mname, qclass, mc,
					    (sc & SCSTATIC) != 0);
					return true;
				}
			}
			/* C++ free-function overload: a same-name declaration with a
			 * different parameter signature is an overload, not a
			 * conflicting redeclaration.  Register it under the
			 * parameter-encoded mangled name `name_<codes>` (mirroring
			 * the member scheme) so all overloads coexist in the scope;
			 * the call site resolves the right one from the argument
			 * types.  The first overload keeps the plain name, so the
			 * plain identifier still resolves for lookup and calls. */
			{
				extern int g_lang;
				extern void cpp_free_mangle_name(const char *, struct type *,
				    char *, size_t);
				const char *regname = name;
				char *mng = NULL;
				if (g_lang == 1 && prior && prior->kind == DECLFUNC &&
				    prior->type && !typecompatible(t, prior->type)) {
					char buf[256];
					/* overloading by return type alone is not allowed */
					if (same_func_params(t, prior->type))
						error(&tok.loc, "function '%s' redeclared with incompatible return type", name);
					cpp_free_mangle_name(name, t, buf, sizeof buf);
					mng = xmalloc(strlen(buf) + 1);
					strcpy(mng, buf);
					regname = mng;
					prior = scopegetdecl(s, mng, false);
				}
				d = declcommon(s, kind, (char *)regname, asmname, t, tq, sc, prior);
				if (mng && d == prior)
					free(mng); /* existing overload: name not retained */
				d->value = mkglobal(d);
				d->u.func.inlinedefn = d->linkage == LINKEXTERN && fs & FUNCINLINE && !(sc & SCEXTERN) && (!prior || prior->u.func.inlinedefn);
				d->u.func.isnoreturn = fs & FUNCNORETURN || a.kind & ATTRNORETURN;
				{
					extern int g_lang;
					/* C++ constexpr function: the QUALCONSTEXPR qualifier is set
					 * by the typequal() handling of the `constexpr` keyword */
					if (g_lang == 1 && (base.qual & QUALCONSTEXPR))
						d->u.func.isconstexpr = true;
				}
				if (tok.kind == TLBRACE) {
					if (!allowfunc)
						error(&tok.loc, "function definition not allowed");
					if (d->defined)
						error(&tok.loc, "function '%s' redefined", name);
					/* re-open scope from function declarator */
					assert(funcscope);
					s = funcscope;
					f = mkfunc(d, (char *)regname, t, s);
					/* C++ constexpr function: buffer the body tokens for
					 * compile-time evaluation, then replay them so the normal
					 * runtime definition is also emitted. */
					{
						extern int g_lang;
						extern void cpp_buffer_constexpr_body(struct decl *);
						if (g_lang == 1 && d->u.func.isconstexpr)
							cpp_buffer_constexpr_body(d);
					}
					stmt(f, s);
					/* C++14 `auto` return type: backfill the type deduced from
					 * the body's return statement(s). */
					{
						extern int g_lang;
						extern struct type typeauto;
						extern struct type *g_cpp_auto_ret_type;
						extern struct func *g_cpp_auto_ret_func;
						if (g_lang == 1 && t->base == &typeauto) {
							if (!g_cpp_auto_ret_type)
								error(&tok.loc, "'auto' function '%s' has no return statement to deduce its type from", name);
							t->base = g_cpp_auto_ret_type;
							g_cpp_auto_ret_type = NULL;
							g_cpp_auto_ret_func = NULL;
						}
					}
					if (d->u.func.isnoreturn)
						funchlt(f);
					/* XXX: need to keep track of function in case a later declaration specifies extern */
					if (!d->u.func.inlinedefn)
						emitfunc(f, d->linkage == LINKEXTERN);
					s = delscope(s);
					delfunc(f);
					d->defined = true;
					return true;
				} else if (funcscope) {
				delscope(funcscope);
			}
			break;
			}
		}
		if (consume(TSEMICOLON))
			return true;
		expect(TCOMMA, "or ';' after declarator");
		allowfunc = 0;
	}
}
struct decl *
stringdecl(struct expr *expr)
{
	static struct map strings;
	struct mapkey key;
	struct decl *d;
	size_t i;

	if (!strings.len)
		mapinit(&strings, 64);
	assert(expr->kind == EXPRSTRING);
	mapkey(&key, expr->u.string.data, expr->u.string.size);
	if (mapput(&strings, &key, &i)) {
		d = mkdecl("string", DECLOBJECT, expr->type, QUALNONE, LINKNONE);
		d->value = mkglobal(d);
		emitdata(d, mkinit(0, expr->type->size, (struct bitfield){0}, expr));
		/* String literals are materialized immediately instead of flowing
		 * through decl()/defineobj().  Record that fact explicitly so no
		 * later tentative-definition sweep can emit the same local symbol
		 * again (especially important during self-host compilation). */
		d->defined = true;
		strings.vals[i].p = d;
	} else {
		d = strings.vals[i].p;
	}
	return d;
}
void
emittentativedefns(void)
{
	struct decl *d;

	for (d = tentativedefns; d; d = d->next) {
		/* stringdecl() emits its private LINKNONE object immediately.  It
		 * is never a C tentative definition; do not let a stale internal
		 * list link turn it into a duplicate .bss symbol during self-host
		 * builds.  Source-level tentative definitions always have linkage. */
		if (!d->defined && !(d->linkage == LINKNONE
			&& strcmp(d->name, "string") == 0))
			defineobj(d, NULL, false, NULL);
	}
}
