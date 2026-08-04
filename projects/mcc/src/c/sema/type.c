#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "util.h"
#include "mcc.h"

#define INTTYPE(k, n, s, p) { \
	.kind = k, .size = n, .align = n, \
	.u.arith = {.issigned = s, .width = n * 8}, \
	.prop = PROPSCALAR|PROPARITH|PROPREAL|PROPINT|p, \
}
#define FLTTYPE(k, n) { \
	.kind = k, .size = n, .align = n, \
	.prop = PROPSCALAR|PROPARITH|PROPREAL|PROPFLOAT, \
}

struct type typevoid    = {.kind = TYPEVOID, .incomplete = true};

/* C++11 `auto` placeholder.  Identified by pointer identity (`&typeauto`);
 * replaced with the deduced type before the declaration is finished, so it
 * never reaches code generation. */
struct type typeauto    = {.kind = TYPEVOID, .align = 1, .size = 1,
                           .incomplete = true};

struct type typebool    = INTTYPE(TYPEBOOL, 1, false, 0);

struct type typechar    = INTTYPE(TYPECHAR, 1, true, PROPCHAR);
struct type typeschar   = INTTYPE(TYPECHAR, 1, true, PROPCHAR);
struct type typeuchar   = INTTYPE(TYPECHAR, 1, false, PROPCHAR);
struct type typechar8   = INTTYPE(TYPECHAR8, 1, false, PROPCHAR);

struct type typeshort   = INTTYPE(TYPESHORT, 2, true, 0);
struct type typeushort  = INTTYPE(TYPESHORT, 2, false, 0);

struct type typeint     = INTTYPE(TYPEINT, 4, true, 0);
struct type typeuint    = INTTYPE(TYPEINT, 4, false, 0);

struct type typelong    = INTTYPE(TYPELONG, 8, true, 0);
struct type typeulong   = INTTYPE(TYPELONG, 8, false, 0);

struct type typellong   = INTTYPE(TYPELLONG, 8, true, 0);
struct type typeullong  = INTTYPE(TYPELLONG, 8, false, 0);

struct type typefloat   = FLTTYPE(TYPEFLOAT, 4);
struct type typedouble  = FLTTYPE(TYPEDOUBLE, 8);
struct type typeldouble = FLTTYPE(TYPELDOUBLE, 16);

struct type typenullptr = {.kind = TYPENULLPTR, .size = 8, .align = 8, .prop = PROPSCALAR};

struct type *typeadjvalist;

struct type *
mktype(enum typekind kind, enum typeprop prop)
{
	struct type *t;

	t = xmalloc(sizeof(*t));
	t->kind = kind;
	t->prop = prop;
	t->qual = QUALNONE;
	t->value = NULL;
	t->incomplete = false;
	t->isref = false;
	t->isrref = false;
	t->scoped = false;
	if (kind == TYPESTRUCT || kind == TYPEUNION) {
		/* C++ virtual-dispatch fields must start zeroed: cpp_class_decl
		 * and the vtable machinery read them before explicit init. */
		t->u.structunion.poly = false;
		t->u.structunion.own_poly = false;
		t->u.structunion.own_virtuals = NULL;
		t->u.structunion.vslots = NULL;
		t->u.structunion.nvslots = 0;
		t->u.structunion.primary_base = NULL;
	}
	if (kind == TYPEFUNC) {
		t->u.func.is_noexcept = false;
	}

	return t;
}

struct type *
mkpointertype(struct type *base, enum typequal qual)
{
	struct type *t;

	t = mktype(TYPEPOINTER, PROPSCALAR);
	t->base = base;
	t->qual = qual;
	/* Pointer size/align follow `long` for the active target: 8 bytes on
	 * LP64 (amd64/arm64/rv64) and 4 bytes on ILP32 (i386). targinit()
	 * adjusts typelong's size/align per target before any pointer type
	 * is constructed, so reading typelong.size here yields the correct
	 * width for the selected ABI. */
	t->size = typelong.size;
	t->align = typelong.align;
	if (base)
		t->prop |= base->prop & PROPVM;

	return t;
}

struct type *
mkarraytype(struct type *base, enum typequal qual, unsigned long long len)
{
	struct type *t;

	t = mktype(TYPEARRAY, 0);
	t->base = base;
	t->qual = qual;
	t->u.array.length = NULL;
	t->u.array.ptrqual = QUALNONE;
	t->incomplete = !len;
	if (t->base) {
		t->align = t->base->align;
		t->size = t->base->size * len;
		/* NOTE: silent overflow is possible for arrays > 2^64 bytes.
		 * A production fix would check for overflow and emit a
		 * compile-time diagnostic (C11 §6.5.5). Not a practical
		 * concern for real-world code. */
	}

	return t;
}

struct type *
mkbitinttype(int width, bool sign)
{
	struct type *t;

	t = mktype(TYPEBITINT, PROPSCALAR | PROPARITH | PROPREAL | PROPINT);
	t->u.arith.issigned = sign;
	t->u.arith.width = width;
	if (width < 1 + sign || width > 64)
		error_code(E_DECL, &tok.loc, "invalid %s _BitInt width %d", sign ? "signed" : "unsigned", width);

	/* calculate byte size */
	t->size = 1;
	while (t->size * 8 < width)
		t->size <<= 1;
	t->align = t->size;

	return t;
}

struct type *
mkatomictype(struct type *base, enum typequal qual)
{
	struct type *t;

	t = mktype(TYPEATOMIC, base->prop & (PROPSCALAR | PROPARITH | PROPINT | PROPFLOAT | PROPREAL));
	t->base = base;
	t->qual = qual;
	t->align = base->align;
	t->size = base->size;
	t->u.atomic.basequal = qual;

	return t;
}

struct type *
mkcomplextype(struct type *base)
{
	struct type *t;

	t = mktype(base->kind, base->prop | PROPCOMPLEX);
	t->base = base;
	t->u.arith.issigned = base->u.arith.issigned;
	t->u.arith.iscomplex = true;
	t->u.arith.width = base->u.arith.width;
	t->size = base->size * 2;  /* real + imag parts */
	t->align = base->align;
	return t;
}

struct type *
mkimaginarytype(struct type *base)
{
	return mkcomplextype(base);
}

struct type *
mkdecimaltype(int kind)
{
	struct type *t;
	int sz = kind == TYPEDECIMAL32 ? 4 : kind == TYPEDECIMAL64 ? 8 : 16;

	t = mktype(kind, PROPSCALAR | PROPREAL);
	t->size = sz;
	t->align = sz;
	return t;
}

/*
We define type rank using the number of bits in the type shifted
left by 7. The least significant 4 bits are used to establish a
ranking between integer types with the same width (such as long and
long long on 64-bit platforms, or int and long on 32-bit platforms).
*/
static int
typerank(struct type *t)
{
	if (t->kind == TYPEENUM)
		t = t->base;
	assert(t->prop & PROPINT);
	switch (t->kind) {
	case TYPEBOOL:   return 0;
	case TYPECHAR:   return 0x081;
	case TYPECHAR8:  return 0x081;
	case TYPESHORT:  return 0x101;
	case TYPEINT:    return 0x201;
	case TYPELONG:   return (t->size << 7) | 0x1;
	case TYPELLONG:  return 0x402;
	case TYPEBITINT: return t->u.arith.width << 4;
	}
	fatal("internal error; unhandled integer type");
	return -1;
}

bool
typecompatible(struct type *t1, struct type *t2)
{
	struct decl *p1, *p2;
	struct expr *e1, *e2;

	if (t1 == t2)
		return true;
	if (t1->kind != t2->kind) {
		/*
		enum types are compatible with their underlying
		type, but not with each other (unless they are the
		same type)
		*/
		if (t1->kind == TYPEENUM && t2 == t1->base)
			return true;
		if (t2->kind == TYPEENUM && t1 == t2->base)
			return true;
		/* nullptr_t is compatible with any pointer type */
		if (t1->kind == TYPENULLPTR && t2->kind == TYPEPOINTER)
			return true;
		if (t2->kind == TYPENULLPTR && t1->kind == TYPEPOINTER)
			return true;
		return false;
	}
	switch (t1->kind) {
	case TYPEBITINT:
		return t1->u.arith.width == t2->u.arith.width && t1->u.arith.issigned == t2->u.arith.issigned;
	case TYPEPOINTER:
		/* C++ references are pointers with isref/isrref markers; a value,
		 * an lvalue reference and an rvalue reference to the same base are
		 * distinct types (`f(T)`, `f(T&)`, `f(T&&)` are separate overloads). */
		if (t1->isref != t2->isref || t1->isrref != t2->isrref)
			return false;
		goto derived;
	case TYPEARRAY:
		if (t1->incomplete || t2->incomplete)
			goto derived;
		e1 = t1->u.array.length;
		e2 = t2->u.array.length;
		if (e1 && e2 && e1->kind == EXPRCONST && e2->kind == EXPRCONST && e1->u.constant.u != e2->u.constant.u)
			return false;
		goto derived;
	case TYPEFUNC:
		if (t1->u.func.isvararg != t2->u.func.isvararg)
			return false;
		/* C11 6.7.6.3p10: an old-style declaration (params == NULL, no
		 * parameter information) is compatible with a prototype; prefer
		 * the prototype's parameter information. */
		if (!t1->u.func.params || !t2->u.func.params)
			goto derived;
	/* Parameter types ignore top-level qualifiers for compatibility
	 * (C11 6.7.6.3p15); compare their unqualified forms. */
	for (p1 = t1->u.func.params, p2 = t2->u.func.params; p1 && p2; p1 = p1->next, p2 = p2->next) {
		if (!typecompatible(typeunqual(p1->type, NULL), typeunqual(p2->type, NULL)))
			return false;
	}
		if (p1 || p2)
			return false;
		goto derived;
	case TYPEATOMIC:
		return typecompatible(t1->base, t2->base);
	case TYPESTRUCT:
	case TYPEUNION:
	case TYPEENUM:
		/* Struct/union/enum types are singletons in mcc: each definition
		 * creates exactly one type object.  Pointer equality is sufficient
		 * for type compatibility (same tag -> same type).
		 * Without this case the switch would fall through to `return false`,
		 * breaking function redeclaration checking whenever a struct/union/
		 * enum appears in parameter lists. */
		return t1 == t2;
	derived:
		return t1->qual == t2->qual && typecompatible(t1->base, t2->base);
	}
	return false;
}

bool
typesame(struct type *t1, struct type *t2)
{
	struct decl *p1, *p2;

	if (t1 == t2)
		return true;
	if (t1->kind != t2->kind)
		return false;

	switch (t1->kind) {
	case TYPECHAR:
	case TYPECHAR8:
	case TYPESHORT:
	case TYPEINT:
	case TYPELONG:
	case TYPELLONG:
	case TYPEBOOL:
	case TYPEFLOAT:
	case TYPEDOUBLE:
	case TYPELDOUBLE:
	case TYPEBITINT:
		return t1->u.arith.issigned == t2->u.arith.issigned
		    && t1->u.arith.width == t2->u.arith.width;
	case TYPEVOID:
	case TYPENULLPTR:
		return true;
	case TYPEPOINTER:
		return t1->qual == t2->qual && typesame(t1->base, t2->base);
	case TYPEARRAY:
		if (t1->incomplete != t2->incomplete)
			return false;
		if (!t1->incomplete && !t2->incomplete && t1->size != t2->size)
			return false;
		return typesame(t1->base, t2->base)
		    && t1->qual == t2->qual;
	case TYPEFUNC:
		if (!typesame(t1->base, t2->base))
			return false;
		if (t1->u.func.isvararg != t2->u.func.isvararg)
			return false;
		p1 = t1->u.func.params;
		p2 = t2->u.func.params;
		while (p1 && p2) {
			if (!typesame(p1->type, p2->type))
				return false;
			p1 = p1->next;
			p2 = p2->next;
		}
		return p1 == NULL && p2 == NULL;
	case TYPESTRUCT:
	case TYPEUNION:
	case TYPEENUM:
		return t1 == t2;
	case TYPEATOMIC:
		return t1->qual == t2->qual && typesame(t1->base, t2->base);
	default:
		return false;
	}
}

struct type *
typecomposite(struct type *t1, struct type *t2)
{
	struct decl *p1, *p2;

	if (typesame(t1, t2))
		return t1;
	if (t1->kind != t2->kind)
		return t1;

	switch (t1->kind) {
	case TYPEARRAY:
		if (t2->incomplete)
			return t1;
		if (t1->incomplete)
			return t2;
		return t1;
	case TYPEFUNC:
		if (t1->u.func.isvararg != t2->u.func.isvararg)
			return t1;
		/* C11 6.7.6.3p10: an old-style declaration is compatible with a
		 * prototype; the composite keeps the prototype's parameters. */
		if (!t1->u.func.params)
			return t2;
		if (!t2->u.func.params)
			return t1;
		p1 = t1->u.func.params;
		p2 = t2->u.func.params;
		while (p1 && p2) {
			if (!typecompatible(p1->type, p2->type))
				return t1;
			p1 = p1->next;
			p2 = p2->next;
		}
		if (p1 || p2)
			return t1;
		return t1;
	case TYPEATOMIC:
		return mkatomictype(
		    typecomposite(t1->base, t2->base),
		    t1->qual | t2->qual);
	default:
		return t1;
	}
}

struct type *
typepromote(struct type *t, unsigned width)
{
	if (t == &typefloat)
		return &typedouble;
	if (t->kind == TYPEBITINT)
		return t;
	if (t->prop & PROPINT && (typerank(t) <= typerank(&typeint) || width <= typeint.size * 8)) {
		if (width == -1)
			width = t->size * 8;
		return width - t->u.arith.issigned < typeint.size * 8 ? &typeint : &typeuint;
	}
	return t;
}

struct type *
typecommonreal(struct type *t1, unsigned w1, struct type *t2, unsigned w2)
{
	struct type *tmp;

	assert(t1->prop & PROPREAL && t2->prop & PROPREAL);
	if (t1 == &typeldouble || t2 == &typeldouble)
		return &typeldouble;
	if (t1 == &typedouble || t2 == &typedouble)
		return &typedouble;
	if (t1 == &typefloat || t2 == &typefloat)
		return &typefloat;
	t1 = typepromote(t1, w1);
	t2 = typepromote(t2, w2);
	if (t1 == t2)
		return t1;
	if (t1->u.arith.issigned == t2->u.arith.issigned)
		return typerank(t1) > typerank(t2) ? t1 : t2;
	if (t1->u.arith.issigned) {
		tmp = t1;
		t1 = t2;
		t2 = tmp;
	}
	/* t1 is unsigned and t2 is signed */
	if (typerank(t1) >= typerank(t2))
		return t1;
	/* t1 has a lower rank than t2 */
	if (t1->u.arith.width < t2->u.arith.width)
		return t2;
	switch (t2->kind) {
	case TYPEINT: return &typeuint;
	case TYPELONG: return &typeulong;
	case TYPELLONG: return &typellong;
	}
	fatal("internal error; could not find common real type");
	return NULL;
}

/* function parameter type adjustment (C11 6.7.6.3p7) */
struct type *
typeadjust(struct type *t, enum typequal *tq)
{
	enum typequal ptrqual;

	switch (t->kind) {
	case TYPEARRAY:
		ptrqual = t->u.array.ptrqual;
		t = mkpointertype(t->base, *tq | t->qual);
		*tq = ptrqual;
		break;
	case TYPEFUNC:
		assert(*tq == QUALNONE);
		t = mkpointertype(t, QUALNONE);
		break;
	}

	return t;
}

struct member *
typemember(struct type *t, const char *name, unsigned long long *offset)
{
	struct member *m, *sub;

	assert(t->kind == TYPESTRUCT || t->kind == TYPEUNION);
	for (m = t->u.structunion.members; m; m = m->next) {
		if (m->name) {
			if (strcmp(m->name, name) == 0) {
				*offset += m->offset;
				return m;
			}
		} else {
			sub = typemember(m->type, name, offset);
			if (sub) {
				*offset += m->offset;
				return sub;
			}
		}
	}
	return NULL;
}

bool
typehasint(struct type *t, unsigned long long i, bool sign)
{
	assert(t->prop & PROPINT);
	/* -1ull << 63 = 0x8000...0 = -2^63 in signed interpretation.
	 * Comparison is unsigned; if i >= this value it may overflow
	 * the signed range and needs a signed-type check below. */
	if (sign && i >= -1ull << 63)
		return t->u.arith.issigned && i >= -1ull << (t->size << 3) - 1;
	return i <= 0xffffffffffffffffull >> (((8 - t->size) << 3) + t->u.arith.issigned);
}

struct type *
typeunqual(struct type *t, enum typequal *q)
{
	if (q)
		*q = t->qual;
	if (t->qual == QUALNONE)
		return t;
	struct type *r = xmalloc(sizeof *r);
	*r = *t;
	r->qual = QUALNONE;
	return r;
}
