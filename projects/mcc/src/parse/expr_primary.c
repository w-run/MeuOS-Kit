/* parse/expr_primary.c -- primary expressions (the leaf of the grammar).
 *
 * Implements primaryexpr() which dispatches on the current token to
 * produce a leaf expression: identifier, constant, string, parenthesised
 * expression, generic selection, builtin call, statement expression, or
 * compound literal. designator() and builtinfunc() are helpers used
 * from initialiser and constant-expression paths respectively. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "mcc.h"
#include "expr_internal.h"

/* designator() is file-local: only builtinfunc() in this translation
 * unit calls it. inttype() lives in expr_literal.c and is exported
 * via expr_internal.h, so no forward decl is needed here. */
static void designator(struct scope *, struct type *, unsigned long long *);

struct expr *
primaryexpr(struct scope *s)
{
	struct expr *e;
	struct decl *d;
	struct type *t;
	char *src, *end;
	uint_least32_t chr;
	unsigned long long val;
	bool hexoct, ordinary;
	int base;

	switch (tok.kind) {
	case TSTRINGLIT:
		e = mkexpr(EXPRSTRING, NULL, NULL);
		t = stringconcat(&e->u.string, false);
		e->type = mkarraytype(t, QUALNONE, e->u.string.size);
		e->lvalue = true;
		e = decay(e);
		break;
	case TCHARCONST:
		src = tok.lit;
		ordinary = false;
		switch (*src) {
		case 'L': ++src; t = targ->typewchar; break;
		case 'u': ++src; t = *src == '8' ? ++src, &typeuchar : &typeushort; break;
		case 'U': ++src; t = &typeuint; break;
		default: t = &typeuchar, ordinary = true;
		}
		assert(*src == '\'');
		++src;
		src += decodechar(src, &chr, &hexoct, "character constant", &tok.loc);
		if (hexoct && !typehasint(t, chr, false))
			error(&tok.loc, "character constant escape is out of range");
		val = chr;
		if (ordinary) {
			if (typechar.u.arith.issigned)
				val = (val ^ 0x80) - 0x80;
			t = &typeint;
		}
		e = mkconstexpr(t, val);
		if (*src != '\'')
			error(&tok.loc, "character constant contains more than one character: %c", *src);
		next();
		break;
	case TNUMBER:
		e = mkexpr(EXPRCONST, NULL, NULL);
		if (tok.lit[0] == '0') {
			switch (tok.lit[1]) {
			case 'x': case 'X': base = 16; break;
			case 'b': case 'B': base = 2; break;
			default: base = 8; break;
			}
		} else {
			base = 10;
		}
		/* C23: 100f / 42F float suffix without '.' or exponent (6.4.4.2).
		 * Only add fF for decimal; hex with .pP already works. */
		if (strpbrk(tok.lit, base == 16 ? ".pP" : ".eEfF")) {
			/* floating constant */
			e->u.constant.f = strtod(tok.lit, &end);
			if (end == tok.lit)
				error(&tok.loc, "invalid floating constant '%s'", tok.lit);
			if (!end[0])
				e->type = &typedouble;
			else if ((end[0] == 'f' || end[0] == 'F') && !end[1])
				e->type = &typefloat;
			else if ((end[0] == 'l' || end[0] == 'L') && !end[1])
				e->type = &typeldouble;
			else
				error(&tok.loc, "invalid floating constant suffix '%s'", end);
		} else {
			src = tok.lit;
			if (base == 2)
				src += 2;
			/* integer constant */
			e->u.constant.u = strtoull(src, &end, base);
			if (end == src)
				error(&tok.loc, "invalid integer constant '%s'", tok.lit);
			e->type = inttype(e->u.constant.u, base == 10, end);
		}
		next();
		break;
	case TTRUE:
	case TFALSE:
		e = mkexpr(EXPRCONST, &typebool, NULL);
		e->u.constant.u = tok.kind == TTRUE;
		next();
		break;
	case TNULLPTR:
		e = mkexpr(EXPRCONST, &typenullptr, NULL);
		e->u.constant.u = 0;
		next();
		break;
	case TLPAREN:
		next();
		if (tok.kind == TLBRACE) {
			/* GNU statement expression ({...}) */
			e = parse_stmt_expr_body(s);
			expect(TRPAREN, "after statement expression");
			break;
		}
		e = expr(s);
		expect(TRPAREN, "after expression");
		break;
	case T_GENERIC:
		e = generic(s);
		break;
	case T__PRAGMA__:
		/* _Pragma("string") — C99/C23 pragma operator, treated as no-op */
		next();
		expect(TLPAREN, "after _Pragma");
		if (tok.kind == TSTRINGLIT) next();
		expect(TRPAREN, "after _Pragma argument");
		e = mkexpr(EXPRCONST, &typevoid, NULL);
		e->u.constant.u = 0;
		break;
	default:
		if (tok.kind >= TIDENT) {
			d = scopegetdecl(s, tokenstr(tok.kind), 1);
			if (!d)
				error(&tok.loc, "undeclared identifier: %s", tokenstr(tok.kind));
			e = mkexpr(EXPRIDENT, d->type, NULL);
			e->qual = d->qual;
			e->lvalue = d->kind == DECLOBJECT;
			e->u.ident.decl = d;
			if (d->kind != DECLBUILTIN)
				e = decay(e);
			next();
			break;
		}
		error(&tok.loc, "expected primary expression");
	}

	return e;
}
static void
designator(struct scope *s, struct type *t, unsigned long long *offset)
{
	char *name;
	struct member *m;
	unsigned long long i;

	for (;;) {
		switch (tok.kind) {
		case TLBRACK:
			if (t->kind != TYPEARRAY)
				error(&tok.loc, "index designator is only valid for array types");
			next();
			i = intconstexpr(s, false);
			expect(TRBRACK, "for index designator");
			t = t->base;
			*offset += i * t->size;
			break;
		case TPERIOD:
			if (t->kind != TYPESTRUCT && t->kind != TYPEUNION)
				error(&tok.loc, "member designator only valid for struct/union types");
			next();
			name = expect(TIDENT, "for member designator");
			m = typemember(t, name, offset);
			if (!m)
				error(&tok.loc, "%s has no member named '%s'", t->kind == TYPEUNION ? "union" : "struct", name);
			t = m->type;
			break;
		default:
			return;
		}
	}
}
struct expr *
builtinfunc(struct scope *s, enum builtinkind kind)
{
	struct expr *e, *toeval;
	struct type *t;
	struct member *m;
	char *name;
	unsigned long long offset;

	switch (kind) {
	case BUILTINALLOCA:
		e = exprassign(assignexpr(s), &typeulong);
		e = mkexpr(EXPRBUILTIN, mkpointertype(&typevoid, QUALNONE), e);
		e->u.builtin.kind = BUILTINALLOCA;
		break;
	case BUILTINCONSTANTP:
		e = mkconstexpr(&typeint, eval(condexpr(s))->kind == EXPRCONST);
		break;
	case BUILTINEXPECT:
		/* just a no-op for now */
		/* TODO: check that the expression and the expected value have type 'long' */
		e = assignexpr(s);
		expect(TCOMMA, "after expression");
		delexpr(assignexpr(s));
		break;
	case BUILTININFF:
		e = mkexpr(EXPRCONST, &typefloat, NULL);
		/* TODO: use INFINITY here when we can handle musl's math.h */
		e->u.constant.f = strtod("inf", NULL);
		break;
	case BUILTINNANF:
		e = assignexpr(s);
		if (!e->decayed || e->base->kind != EXPRSTRING || e->base->u.string.size > 1)
			error(&tok.loc, "__builtin_nanf currently only supports empty string literals");
		e = mkexpr(EXPRCONST, &typefloat, NULL);
		/* TODO: use NAN here when we can handle musl's math.h */
		e->u.constant.f = strtod("nan", NULL);
		break;
	case BUILTINOFFSETOF:
		t = typename(s, NULL, NULL);
		expect(TCOMMA, "after type name");
		name = expect(TIDENT, "after ','");
		if (t->kind != TYPESTRUCT && t->kind != TYPEUNION)
			error(&tok.loc, "type is not a struct/union type");
		offset = 0;
		m = typemember(t, name, &offset);
		if (!m)
			error(&tok.loc, "struct/union has no member named '%s'", name);
		designator(s, m->type, &offset);
		e = mkconstexpr(&typeulong, offset);
		break;
	case BUILTINTYPESCOMPATIBLEP:
		t = typename(s, NULL, NULL);
		expect(TCOMMA, "after type name");
		e = mkconstexpr(&typeint, typecompatible(t, typename(s, NULL, NULL)));
		break;
	case BUILTINUNREACHABLE:
		e = mkexpr(EXPRBUILTIN, &typevoid, NULL);
		e->u.builtin.kind = BUILTINUNREACHABLE;
		break;
	case BUILTINVAARG:
		e = mkexpr(EXPRBUILTIN, NULL, assignexpr(s));
		e->u.builtin.kind = BUILTINVAARG;
		if (!typesame(e->base->type, typeadjvalist))
			error(&tok.loc, "va_arg argument must have type va_list");
		if (typeadjvalist == targ->typevalist)
			e->base = mkunaryexpr(TBAND, e->base);
		expect(TCOMMA, "after va_list");
		e->type = typename(s, &e->qual, &toeval);
		e->toeval = toeval;
		break;
	case BUILTINVACOPY:
		e = mkexpr(EXPRASSIGN, &typevoid, NULL);
		e->u.assign.l = assignexpr(s);
		if (!typesame(e->u.assign.l->type, typeadjvalist))
			error(&tok.loc, "va_copy destination must have type va_list");
		if (typeadjvalist != targ->typevalist)
			e->u.assign.l = mkunaryexpr(TMUL, e->u.assign.l);
		expect(TCOMMA, "after target va_list");
		e->u.assign.r = assignexpr(s);
		if (!typesame(e->u.assign.r->type, typeadjvalist))
			error(&tok.loc, "va_copy source must have type va_list");
		if (typeadjvalist != targ->typevalist)
			e->u.assign.r = mkunaryexpr(TMUL, e->u.assign.r);
		break;
	case BUILTINVAEND:
		e = assignexpr(s);
		if (!typesame(e->type, typeadjvalist))
			error(&tok.loc, "va_end argument must have type va_list");
		e = mkexpr(EXPRCAST, &typevoid, e);
		break;
	case BUILTINVASTART:
		e = mkexpr(EXPRBUILTIN, &typevoid, assignexpr(s));
		e->u.builtin.kind = BUILTINVASTART;
		if (!typesame(e->base->type, typeadjvalist))
			error(&tok.loc, "va_start argument must have type va_list");
		if (typeadjvalist == targ->typevalist)
			e->base = mkunaryexpr(TBAND, e->base);
		if (consume(TCOMMA))
			delexpr(assignexpr(s));
		break;
	case BUILTINATOMICFETCHADD:
	case BUILTINATOMICFETCHSUB:
	case BUILTINATOMICFETCHAND:
	case BUILTINATOMICFETCHOR:
	case BUILTINATOMICFETCHXOR:
	case BUILTINATOMICEXCHANGE:
		/* __builtin_atomic_fetch_{add,sub}(ptr, value, memory_order).
		 * The memory-order expression is deliberately parsed and discarded:
		 * this first compiler-runtime lowering provides seq_cst semantics. */
		e = mkexpr(EXPRBUILTIN, NULL, assignexpr(s));
		if (e->base->type->kind != TYPEPOINTER ||
		    !(e->base->type->qual & QUALATOMIC))
			error(&tok.loc, "atomic fetch operation requires pointer to _Atomic object");
		e->type = e->base->type->base;
		expect(TCOMMA, "after atomic object");
		e->base->next = exprassign(assignexpr(s), e->type);
		expect(TCOMMA, "after atomic operand");
		delexpr(assignexpr(s));
		e->u.builtin.kind = kind;
		break;
	case BUILTINATOMICCOMPAREEXCHANGE:
		/* (object, expected-pointer, desired, weak, success-order,
		 * failure-order).  The runtime writes the observed value through
		 * expected on failure, as required by C11. */
		e = mkexpr(EXPRBUILTIN, &typeint, assignexpr(s));
		if (e->base->type->kind != TYPEPOINTER ||
		    !(e->base->type->qual & QUALATOMIC))
			error(&tok.loc, "atomic compare exchange requires pointer to _Atomic object");
		expect(TCOMMA, "after atomic object");
		e->base->next = assignexpr(s);
		if (e->base->next->type->kind != TYPEPOINTER)
			error(&tok.loc, "atomic compare exchange expected argument must be a pointer");
		expect(TCOMMA, "after expected value");
		e->base->next->next = exprassign(assignexpr(s), e->base->type->base);
		expect(TCOMMA, "after desired value");
		delexpr(assignexpr(s)); /* weak */
		expect(TCOMMA, "after weak argument");
		delexpr(assignexpr(s)); /* success order */
		expect(TCOMMA, "after success memory order");
		delexpr(assignexpr(s)); /* failure order */
		e->u.builtin.kind = kind;
		break;
	default:
		fatal("internal error; unknown builtin");
	}
	return e;
}
