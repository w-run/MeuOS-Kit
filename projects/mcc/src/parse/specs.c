/* parse/specs.c -- declaration specifiers (storage/qual/func/type).
 *
 * Implements the "decl-specifier" half of the C declaration grammar:
 *   storageclass()    -> storage-class specifiers (extern/static/...)
 *   typequal()        -> type qualifiers (const/volatile/restrict/_Atomic)
 *   funcspec()        -> function specifiers (inline/_Noreturn)
 *   tagspec()         -> struct/union/enum tags
 *   declspecs()       -> the combined decl-specifier-sequence parser
 *   typename()        -> reads a type name used in sizeof/casts/etc.
 *   istypename()      -> lookahead helper used to disambiguate casts
 *
 * These all consume a token stream via tok and call into sema/type.c
 * to construct type nodes. The other half (declarators) lives in
 * declarator.c; the forward decls below let declspecs/typename refer
 * to functions defined there without exposing them globally. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "mcc.h"
#include "decl_internal.h"
#include <limits.h>

int
storageclass(enum storageclass *sc)
{
	enum storageclass allowed, new;

	switch (tok.kind) {
	case TTYPEDEF:       new = SCTYPEDEF;     break;
	case TEXTERN:        new = SCEXTERN;      break;
	case TSTATIC:        new = SCSTATIC;      break;
	case TTHREAD_LOCAL:  new = SCTHREADLOCAL; break;
	case TREGISTER:      new = SCREGISTER;    break;
	/* C23: TAUTO is handled in declspecs() as a type deduction indicator */
	// case TAUTO:      new = SCAUTO;        break;
	default: return 0;
	}
	if (!sc)
		error(&tok.loc, "storage class not allowed in this declaration");
	switch (*sc) {
	case SCNONE:        allowed = ~SCNONE;           break;
	case SCTHREADLOCAL: allowed = SCSTATIC|SCEXTERN; break;
	case SCSTATIC:
	case SCEXTERN:      allowed = SCTHREADLOCAL;     break;
	default:            allowed = SCNONE;            break;
	}
	if (new & ~allowed)
		error(&tok.loc, "invalid combination of storage class specifiers");
	*sc |= new;
	next();

	return 1;
}
int
typequal(enum typequal *tq)
{
	switch (tok.kind) {
	case TCONST:    *tq |= QUALCONST;    break;
	case TVOLATILE: *tq |= QUALVOLATILE; break;
	case TRESTRICT: *tq |= QUALRESTRICT; break;
	case T_ATOMIC:  *tq |= QUALATOMIC;  break;
	case TCONSTEXPR:
		*tq |= QUALCONST | QUALCONSTEXPR;  /* C23 constexpr */
		break;
	default: return 0;
	}
	next();

	return 1;
}
int
funcspec(enum funcspec *fs)
{
	enum funcspec new;

	switch (tok.kind) {
	case TINLINE:    new = FUNCINLINE;   break;
	case T_NORETURN: new = FUNCNORETURN; break;
	default: return 0;
	}
	if (!fs)
		error(&tok.loc, "function specifier not allowed in this declaration");
	*fs |= new;
	next();

	return 1;
}
struct type *
tagspec(struct scope *s)
{
	static struct type *const inttypes[][2] = {
		{&typeuint, &typeint},
		{&typeulong, &typelong},
		{&typeullong, &typellong},
	};
	struct type *t, *et;
	char *tag, *name;
	enum typekind kind;
	struct decl *d, *enumconsts;
	struct expr *e;
	struct attr a;
	enum attrkind allowedattr;
	struct structbuilder b;
	unsigned long long value, max, min;
	bool sign;
	int i;

	allowedattr = 0;
	switch (tok.kind) {
	case TSTRUCT: kind = TYPESTRUCT, allowedattr |= ATTRPACKED; break;
	case TUNION:  kind = TYPEUNION, allowedattr |= ATTRPACKED; break;
	case TENUM:   kind = TYPEENUM; break;
	default: fatal("internal error: unknown tag kind");
	}
	next();
	a.kind = 0;
	attr(&a, allowedattr);
	gnuattr(&a, allowedattr);
	tag = 0;
	t = NULL;
	et = NULL;
	if (tok.kind >= TIDENT) {
		tag = tokenstr(tok.kind);
		next();
	}
	if (kind == TYPEENUM && consume(TCOLON)) {
		et = declspecs(s, NULL, NULL, NULL).type;
		if (!et)
			error(&tok.loc, "no type in enum type specifier");
	}
	if (tag)
		t = scopegettag(s, tag, tok.kind != TLBRACE && tok.kind != TSEMICOLON);
	if (t) {
		if (t->kind != kind)
			error(&tok.loc, "redeclaration of tag '%s' with different kind", tag);
	} else {
		if (kind == TYPEENUM) {
			t = mktype(kind, PROPSCALAR|PROPARITH|PROPREAL|PROPINT);
			t->base = et;
		} else {
			t = mktype(kind, 0);
			t->size = 0;
			t->align = 0;
			t->u.structunion.tag = tag;
			t->u.structunion.members = NULL;
		}
		t->incomplete = true;
		if (tag)
			scopeputtag(s, tag, t);
	}
	if (tok.kind != TLBRACE)
		return t;
	if (!t->incomplete)
		error(&tok.loc, "redefinition of tag '%s'", tag);
	next();
	switch (t->kind) {
	case TYPESTRUCT:
	case TYPEUNION:
		b.type = t;
		b.last = &t->u.structunion.members;
		b.bits = 0;
		b.pack = a.kind & ATTRPACKED;
		do structdecl(s, &b);
		while (tok.kind != TRBRACE);
		if (!t->u.structunion.members)
			error(&tok.loc, "struct/union has no members");
		next();
		if (!b.pack)
			t->size = ALIGNUP(t->size, t->align);
		break;
	case TYPEENUM:
		enumconsts = NULL;
		if (et) {
			t->size = t->base->size;
			t->align = t->base->align;
			t->u.arith.issigned = t->base->u.arith.issigned;
			t->u.arith.width = t->base->u.arith.width;
			t->incomplete = false;
			et = t;
		} else {
			et = &typeint;
		}
		max = 0;
		min = 0;
		for (value = 0; tok.kind >= TIDENT; ++value) {
			name = tokenstr(tok.kind);
			next();
			attr(NULL, 0);
			if (consume(TASSIGN)) {
				e = eval(condexpr(s));
				if (e->kind != EXPRCONST || !(e->type->prop & PROPINT))
					error(&tok.loc, "expected integer constant expression");
				value = e->u.constant.u;
				if (!t->base)
					et = typehasint(&typeint, value, e->type->u.arith.issigned) ? &typeint : e->type;
				else if (!typehasint(et, value, e->type->u.arith.issigned))
					goto invalid;
			} else if (value == 0 && !et->u.arith.issigned || value == 1ull << 63 && et->u.arith.issigned) {
				error(&tok.loc, "no %ssigned integer type can represent enumerator value", et->u.arith.issigned ? "" : "un");
			} else if (!typehasint(et, value, et->u.arith.issigned)) {
				if (t->base) {
				invalid:
					/* fixed underlying type */
					error(&tok.loc, "enumerator '%s' value cannot be represented in underlying type", name);
				}
				sign = et->u.arith.issigned;
				for (i = 0; i < countof(inttypes); ++i) {
					et = inttypes[i][sign];
					if (typehasint(et, value, sign))
						break;
				}
				assert(i < countof(inttypes));
			}
			d = mkdecl(name, DECLCONST, et, QUALNONE, LINKNONE);
			d->u.enumconst = value;
			d->value = mkintconst(value);
			d->next = enumconsts;
			enumconsts = d;
			if (et->u.arith.issigned && value >= 1ull << 63) {
				if (-value > min)
					min = -value;
			} else if (value > max) {
				max = value;
			}
			scopeputdecl(s, d);
			if (!consume(TCOMMA))
				break;
		}
		expect(TRBRACE, "to close enum specifier");
		if (!t->base) {
			if (min <= 0x80000000 && max <= 0x7fffffff) {
				t->base = min ? &typeint : &typeuint;
			} else {
				sign = min > 0;
				for (i = 0; i < countof(inttypes); ++i) {
					et = inttypes[i][sign];
					if (typehasint(et, max, false) && typehasint(et, -min, true))
						break;
				}
				if (i == countof(inttypes))
					error(&tok.loc, "no integer type can represent all enumerator values");
				t->base = et;
				for (d = enumconsts; d; d = d->next)
					d->type = t;
			}
			t->size = t->base->size;
			t->align = t->base->align;
			t->u.arith.issigned = t->base->u.arith.issigned;
			t->u.arith.width = t->base->u.arith.width;
		}
	}
	t->incomplete = false;

	return t;
}
struct qualtype
declspecs(struct scope *s, enum storageclass *sc, enum funcspec *fs, int *align)
{
	struct type *t, *other;
	struct decl *d;
	struct expr *e;
	enum typespec ts = SPECNONE;
	enum typequal tq = QUALNONE;
	enum tokenkind op;
	int ntypes = 0;
	unsigned long long i, bits;
	struct expr *typeofexpr = NULL;

	t = NULL;
	if (sc)
		*sc = SCNONE;
	if (fs)
		*fs = FUNCNONE;
	if (align)
		*align = 0;
	for (;;) {
		/* `_Atomic(type-name)` is a type specifier, unlike `_Atomic`
		 * followed by ordinary declaration specifiers, which is a
		 * qualifier. */
		if (tok.kind == T_ATOMIC && peek(TLPAREN)) {
			enum typequal aq = QUALNONE;
			struct type *atype;

			/* peek() has already consumed _Atomic and (, so
			 * tok is now at the type name. */
			atype = typename(s, &aq, NULL);
			if (atype) {
				t = mkatomictype(atype, aq);
				tq |= QUALATOMIC;
				++ntypes;
				expect(TRPAREN, "to close _Atomic type name");
			} else {
				error(&tok.loc, "expected type name in '_Atomic(...)'");
			}
			continue;
		}
		if (typequal(&tq) || storageclass(sc) || funcspec(fs))
			continue;
		op = tok.kind;
		switch (op) {
		/* 6.7.2 Type specifiers */
		case TVOID:
			t = &typevoid;
			++ntypes;
			next();
			break;
		case TCHAR:
			ts |= SPECCHAR;
			++ntypes;
			next();
			break;
		case TSHORT:
			if (ts & SPECSHORT)
				error(&tok.loc, "duplicate 'short'");
			ts |= SPECSHORT;
			next();
			break;
		case TINT:
			ts |= SPECINT;
			++ntypes;
			next();
			break;
		case TLONG:
			if (ts & SPECLONG2)
				error(&tok.loc, "too many 'long'");
			if (ts & SPECLONG)
				ts |= SPECLONG2;
			ts |= SPECLONG;
			next();
			break;
		case TFLOAT:
			ts |= SPECFLOAT;
			++ntypes;
			next();
			break;
		case TDOUBLE:
			ts |= SPECDOUBLE;
			++ntypes;
			next();
			break;
		case TSIGNED:
			if (ts & SPECSIGNED)
				error(&tok.loc, "duplicate 'signed'");
			ts |= SPECSIGNED;
			next();
			break;
		case TUNSIGNED:
			if (ts & SPECUNSIGNED)
				error(&tok.loc, "duplicate 'unsigned'");
			ts |= SPECUNSIGNED;
			next();
			break;
		case T_BITINT:
			ts |= SPECBITINT;
			++ntypes;
			next();
			expect(TLPAREN, "after _BitInt");
			bits = intconstexpr(s, false);
			expect(TRPAREN, "after _BitInt width");
			break;
		case TBOOL:
			t = &typebool;
			++ntypes;
			next();
			break;
		case TAUTO:
			/* C23: auto as type deduction indicator.
			 * Minimal: auto = int for now. */
			ts |= SPECINT;
			++ntypes;
			next();
			break;
		case T_COMPLEX:
			ts |= SPECCOMPLEX;
			/* _Complex is a modifier, not a type - don't increment ntypes */
			next();
			break;
		case T_IMAGINARY:
			ts |= SPECIMAGINARY;
			/* _Imaginary is a modifier, not a type */
			next();
			break;
		case T_DECIMAL32:
			t = mkdecimaltype(TYPEDECIMAL32);
			++ntypes;
			next();
			break;
		case T_DECIMAL64:
			t = mkdecimaltype(TYPEDECIMAL64);
			++ntypes;
			next();
			break;
		case T_DECIMAL128:
			t = mkdecimaltype(TYPEDECIMAL128);
			++ntypes;
			next();
			break;
	case T_ATOMIC:
		/* _Atomic as a qualifier is handled by typequal() above,
		 * and _Atomic(type-name) is handled by the pre-check above.
		 * This case should only be reached for invalid placements. */
		error(&tok.loc, "'_Atomic' may not appear here; use '_Atomic(T)' or '_Atomic T'");
		break;
		case TSTRUCT:
		case TUNION:
		case TENUM:
			t = tagspec(s);
			++ntypes;
			break;
		case TTYPEOF:
		case TTYPEOF_UNQUAL:
			next();
			expect(TLPAREN, "after 'typeof'");
			t = typename(s, &tq, &typeofexpr);
			if (!t) {
				e = expr(s);
				if (e->decayed)
					e = e->base;
				t = e->type;
				if (op == TTYPEOF)
					tq |= e->qual;
				if (t->prop & PROPVM)
					typeofexpr = e;
			}
			++ntypes;
			expect(TRPAREN, "to close 'typeof'");
			break;

		/* 6.7.5 Alignment specifier */
		case TALIGNAS:
			if (!align)
				error(&tok.loc, "alignment specifier not allowed in this declaration");
			next();
			expect(TLPAREN, "after 'alignas'");
			other = typename(s, NULL, NULL);
			i = other ? other->align : intconstexpr(s, false);
			if (i & i - 1 || i > INT_MAX)
				error(&tok.loc, "invalid alignment: %llu", i);
			if (i > *align)
				*align = i;
			expect(TRPAREN, "to close 'alignas' specifier");
			break;

		case T__ATTRIBUTE__:
			gnuattr(NULL, (enum attrkind)(ATTRWEAK | ATTRUSED | ATTRNOINLINE | ATTRALWAYSINLINE |
			    ATTRCONSTRUCTOR | ATTRDESTRUCTOR | ATTRSECTION | ATTRALIGNED | ATTRPACKED |
			    ATTRNORETURN | ATTRDEPRECATED));
			break;

		case T__EXTENSION__:
			next();
			break;

		default:
			if (op < TIDENT || t || ts)
				goto done;
			d = scopegetdecl(s, tokenstr(tok.kind), 1);
			if (!d || d->kind != DECLTYPE)
				goto done;
			t = d->type;
			tq |= d->qual;
			++ntypes;
			next();
			break;
		}
		if (ntypes > 1 || (t && ts))
			error(&tok.loc, "multiple types in declaration specifiers");
	}
done:
	/* Strip complex/imaginary modifier bits before the type-combination
	 * switch -- they are handled as wrappers below. */
	ts &= ~(SPECCOMPLEX | SPECIMAGINARY);
	switch ((int)ts) {
	case SPECNONE:                                            break;
	case SPECCHAR:                          t = &typechar;    break;
	case SPECSIGNED|SPECCHAR:               t = &typeschar;   break;
	case SPECUNSIGNED|SPECCHAR:             t = &typeuchar;   break;
	case SPECSHORT:
	case SPECSHORT|SPECINT:
	case SPECSIGNED|SPECSHORT:
	case SPECSIGNED|SPECSHORT|SPECINT:      t = &typeshort;   break;
	case SPECUNSIGNED|SPECSHORT:
	case SPECUNSIGNED|SPECSHORT|SPECINT:    t = &typeushort;  break;
	case SPECINT:
	case SPECSIGNED:
	case SPECSIGNED|SPECINT:                t = &typeint;     break;
	case SPECUNSIGNED:
	case SPECUNSIGNED|SPECINT:              t = &typeuint;    break;
	case SPECLONG:
	case SPECLONG|SPECINT:
	case SPECSIGNED|SPECLONG:
	case SPECSIGNED|SPECLONG|SPECINT:       t = &typelong;    break;
	case SPECUNSIGNED|SPECLONG:
	case SPECUNSIGNED|SPECLONG|SPECINT:     t = &typeulong;   break;
	case SPECLONGLONG:
	case SPECLONGLONG|SPECINT:
	case SPECSIGNED|SPECLONGLONG:
	case SPECSIGNED|SPECLONGLONG|SPECINT:   t = &typellong;   break;
	case SPECUNSIGNED|SPECLONGLONG:
	case SPECUNSIGNED|SPECLONGLONG|SPECINT: t = &typeullong;  break;
	case SPECBITINT:
	case SPECBITINT|SPECSIGNED:             t = mkbitinttype(bits, true); break;
	case SPECBITINT|SPECUNSIGNED:           t = mkbitinttype(bits, false); break;
	case SPECFLOAT:                         t = &typefloat;   break;
	case SPECDOUBLE:                        t = &typedouble;  break;
	case SPECLONG|SPECDOUBLE:               t = &typeldouble; break;
	default:
		error(&tok.loc, "invalid combination of type specifiers");
	}
	if (tq & QUALRESTRICT) {
		/* C23 6.7.4.1p2 */
		other = t;
		while (other->kind == TYPEARRAY)
			other = other->base;
		if (!other || other->kind != TYPEPOINTER)
			error(&tok.loc, "'restrict' applied to non-pointer type");
		if (other->base->kind == TYPEFUNC)
			error(&tok.loc, "'restrict' applied to function pointer");
	}
	if (!t && (tq || sc && *sc || fs && *fs))
		error(&tok.loc, "declaration has no type specifier");
	/* _Complex / _Imaginary type wrapping */
	if (ts & SPECCOMPLEX) {
		if (t && (t->prop & PROPFLOAT))
			t = mkcomplextype(t);
		else
			error(&tok.loc, "_Complex requires a floating point type");
	}
	if (ts & SPECIMAGINARY) {
		if (t && (t->prop & PROPFLOAT))
			t = mkimaginarytype(t);
		else
			error(&tok.loc, "_Imaginary requires a floating point type");
	}
	/*
	TODO: consider delaying attribute parsing to declarator(),
	so we can tell the difference between the start of an
	attribute and an array declarator.
	*/
	attr(NULL, 0);

	return (struct qualtype){t, tq, typeofexpr};
}
bool
istypename(struct scope *s, const char *name)
{
	struct decl *d;

	d = scopegetdecl(s, name, 1);
	return d && d->kind == DECLTYPE;
}
struct type *
typename(struct scope *s, enum typequal *tq, struct expr **toeval)
{
	struct qualtype t;

	t = declspecs(s, NULL, NULL, NULL);
	if (t.type) {
		t = declarator(s, t, NULL, NULL, NULL, true);
		if (tq)
			*tq |= t.qual;
		if (toeval)
			*toeval = t.expr;
	}
	return t.type;
}
