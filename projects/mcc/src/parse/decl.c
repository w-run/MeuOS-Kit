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
struct decl *tentativedefns, **tentativedefnsend = &tentativedefns;

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

	if (prior) {
		if (prior->linkage == LINKNONE)
			error(&tok.loc, "%s '%s' with no linkage redeclared", kindstr, name);
		linkage = getlinkage(kind, sc, prior, s == &filescope);
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
	linkage = getlinkage(kind, sc, prior, s == &filescope);
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
	/* C23: constexpr variable must have a constant expression initializer */
	if ((d->qual & QUALCONSTEXPR) && hasinit && init->expr) {
		struct expr *e = eval(init->expr);
		if (e->kind != EXPRCONST)
			error(&tok.loc, "constexpr variable '%s' requires a constant expression initializer", d->name);
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
		qt = declarator(s, base, &name, NULL, &funcscope, false);
		t = qt.type;
		tq = qt.qual;
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
			init = NULL;
			hasinit = false;
			if (consume(TASSIGN)) {
				if (f && d->linkage != LINKNONE)
					error(&tok.loc, "object '%s' with block scope and %s linkage cannot have initializer", name, d->linkage == LINKEXTERN ? "external" : "internal");
				if (d->defined)
					error(&tok.loc, "object '%s' redefined", name);
				init = parseinit(s, d->type);
				hasinit = true;
			} else if (sc & SCEXTERN) {
				break;
			} else if (d->linkage != LINKNONE && d->u.obj.storage == SDSTATIC) {
				if (!d->defined && !d->tentative) {
					d->tentative = true;
					*tentativedefnsend = d;
					tentativedefnsend = &d->next;
				}
				break;
			}
			defineobj(d, init, hasinit, f);
			break;
		case DECLFUNC:
			if (align)
				error(&tok.loc, "function '%s' declared with alignment specifier", name);
			if (f && sc && sc != SCEXTERN)  /* 6.7.1p7 */
				error(&tok.loc, "function '%s' with block scope may only have storage class 'extern'", name);
			d = declcommon(s, kind, name, asmname, t, tq, sc, prior);
			d->value = mkglobal(d);
			d->u.func.inlinedefn = d->linkage == LINKEXTERN && fs & FUNCINLINE && !(sc & SCEXTERN) && (!prior || prior->u.func.inlinedefn);
			d->u.func.isnoreturn = fs & FUNCNORETURN || a.kind & ATTRNORETURN;
			if (tok.kind == TLBRACE) {
				if (!allowfunc)
					error(&tok.loc, "function definition not allowed");
				if (d->defined)
					error(&tok.loc, "function '%s' redefined", name);
				/* re-open scope from function declarator */
				assert(funcscope);
				s = funcscope;
				f = mkfunc(d, name, t, s);
				stmt(f, s);
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
