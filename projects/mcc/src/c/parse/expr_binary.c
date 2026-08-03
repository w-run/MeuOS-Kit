/* parse/expr_binary.c -- binary and conditional expressions.
 *
 * Implements:
 *   precedence()    -> operator precedence lookup table
 *   binaryexpr()    -> precedence-climbing parser for binary operators
 *   condexpr()      -> the ternary ?: operator
 *
 * The actual binary tree construction lives in mkbinaryexpr() (in
 * expr.c) which is called from binaryexpr(). */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "util.h"
#include "mcc.h"
#include "expr_internal.h"

struct expr *mkexpr(enum exprkind, struct type *, struct expr *);
struct expr *exprconvert(struct expr *, struct type *);
struct expr *mkbinaryexpr(struct location *, enum tokenkind, struct expr *, struct expr *);
struct expr *assignexpr(struct scope *);
struct expr *castexpr(struct scope *);
struct expr *condexpr(struct scope *);
struct expr *binaryexpr(struct scope *, struct expr *, int);

struct expr *binaryexpr(struct scope *, struct expr *, int);
struct expr *mkbinaryexpr(struct location *, enum tokenkind, struct expr *, struct expr *);
struct expr *exprconvert(struct expr *, struct type *);
struct expr *mkexpr(enum exprkind, struct type *, struct expr *);
struct expr *assignexpr(struct scope *);
struct expr *castexpr(struct scope *);

static int
precedence(enum tokenkind t)
{
	switch (t) {
	case TLOR:     return 0;
	case TLAND:    return 1;
	case TBOR:     return 2;
	case TXOR:     return 3;
	case TBAND:    return 4;
	case TEQL:
	case TNEQ:     return 5;
	case TLESS:
	case TGREATER:
	case TLEQ:
	case TGEQ:
	case TSPACESHIP: return 6;
	case TSHL:
	case TSHR:     return 7;
	case TADD:
	case TSUB:     return 8;
	case TMUL:
	case TDIV:
	case TMOD:     return 9;
	}
	return -1;
}
struct expr *
binaryexpr(struct scope *s, struct expr *l, int i)
{
	struct expr *r;
	struct location loc;
	enum tokenkind op;
	int j, k;

	if (!l)
		l = castexpr(s);
	while ((j = precedence(tok.kind)) >= i) {
		op = tok.kind;
		loc = tok.loc;
		next();
		r = castexpr(s);
		while ((k = precedence(tok.kind)) > j)
			r = binaryexpr(s, r, k);
		/* C++ operator overloading: `a + b` lowers to `a.operator+(b)`
		 * when the left operand is a class with that operator. */
		{
			extern int g_lang;
			extern bool cpp_try_operator_call(struct scope *,
			    struct expr *, enum tokenkind, struct expr *,
			    struct expr **);
			struct expr *ocall = NULL;
			if (g_lang == 1 && cpp_try_operator_call(s, l, op, r, &ocall))
				l = ocall;
			else
				l = mkbinaryexpr(&loc, op, l, r);
		}
	}
	return l;
}
struct expr *
condexpr(struct scope *s)
{
	struct expr *e, *l, *r;
	struct type *t, *lt, *rt;
	enum typequal tq;

	e = binaryexpr(s, NULL, 0);
	if (!consume(TQUESTION))
		return e;
	l = tok.kind == TCOLON ? e : expr(s);
	expect(TCOLON, "in conditional expression");
	r = condexpr(s);

	lt = l->type;
	rt = r->type;
	if (lt == rt) {
		t = lt;
	} else if (lt->prop & PROPARITH && rt->prop & PROPARITH) {
		t = typecommonreal(lt, bitfieldwidth(l), rt, bitfieldwidth(r));
	} else if (lt == &typevoid && rt == &typevoid) {
		t = &typevoid;
	} else {
		l = eval(l);
		r = eval(r);
		if (nullpointer(l) && rt->kind == TYPEPOINTER) {
			t = rt;
		} else if (nullpointer(r) && lt->kind == TYPEPOINTER) {
			t = lt;
		} else if (lt->kind == TYPEPOINTER && rt->kind == TYPEPOINTER) {
			tq = lt->qual | rt->qual;
			lt = lt->base;
			rt = rt->base;
			if (lt == &typevoid || rt == &typevoid) {
				t = &typevoid;
			} else if (typecompatible(lt, rt)) {
				t = typecomposite(lt, rt);
			} else {
				error_code(E_CTYPE, &tok.loc, "operands of conditional operator must have compatible types");
			}
			t = mkpointertype(t, tq);
		} else {
			error_code(E_CTYPE, &tok.loc, "invalid operands to conditional operator");
		}
	}
	e = eval(e);
	if (e->kind == EXPRCONST && e->type->prop & PROPINT)
		return exprconvert(e->u.constant.u ? l : r, t);
	e = mkexpr(EXPRCOND, t, e);
	e->u.cond.t = l;
	e->u.cond.f = r;
	return e;
}
