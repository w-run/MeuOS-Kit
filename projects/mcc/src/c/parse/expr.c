/* parse/expr.c -- top-level expression parser + expression constructors.
 *
 * Splits the original monolithic src/parse/expr.c into one file per
 * grammar tier, plus a shared "constructors + entry" file:
 *
 *   expr.c          this file: mkexpr/delexpr/mkconstexpr/decay/...
 *                  expr(), assignexpr(), mkassignexpr()  (entry points)
 *   expr_literal.c  inttype/octval/hexval/decodechar/encodechar-N/stringconcat
 *   expr_primary.c  primaryexpr/designator/builtinfunc
 *   expr_postfix.c  mkincdecexpr/postfixexpr
 *   expr_unary.c    unaryexpr/castexpr
 *   expr_binary.c   precedence/binaryexpr/condexpr
 *   expr_generic.c  generic/intconstexpr
 *
 * The split mirrors the C grammar's precedence climbing; expr() at the
 * bottom calls assignexpr() which calls condexpr() which calls binaryexpr()
 * which calls unaryexpr() which calls postfixexpr() which calls primaryexpr().
 * Tier-specific helpers that cross files (e.g. primaryexpr calls decodechar)
 * are resolved through forward decls in this file. */
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

/* Forward declarations for functions defined in the sibling expr_*.c
 * files. Keeping these here lets the entry points in this file refer to
 * them without dragging the whole C grammar into one translation unit. */
struct expr *primaryexpr(struct scope *);
struct expr *postfixexpr(struct scope *, struct expr *);
struct expr *unaryexpr(struct scope *);
struct expr *castexpr(struct scope *);
struct expr *binaryexpr(struct scope *, struct expr *, int);
struct expr *condexpr(struct scope *);
struct expr *generic(struct scope *);
unsigned long long intconstexpr(struct scope *, bool);
struct expr *builtinfunc(struct scope *, enum builtinkind);
struct type *stringconcat(struct stringlit *, bool);
struct expr *mkunaryexpr(enum tokenkind, struct expr *);
struct expr *mksizeofexpr(struct type *);
struct expr *mkbinaryexpr(struct location *, enum tokenkind, struct expr *, struct expr *);
struct expr *assignexpr(struct scope *);
struct expr *mkassignexpr(struct expr *, struct expr *);

size_t decodechar(const char *, uint_least32_t *, bool *, const char *, struct location *);

struct expr *
mkexpr(enum exprkind k, struct type *t, struct expr *b)
{
	struct expr *e;

	e = xmalloc(sizeof(*e));
	e->qual = QUALNONE;
	e->type = t;
	e->lvalue = false;
	e->decayed = false;
	e->kind = k;
	e->base = b;
	e->next = NULL;
	e->toeval = NULL;

	return e;
}
void
delexpr(struct expr *e)
{
	struct expr *sub;

	switch (e->kind) {
	case EXPRCALL:
		delexpr(e->base);
		while (sub = e->u.call.args) {
			e->u.call.args = sub->next;
			delexpr(sub);
		}
		break;
	case EXPRBITFIELD:
	case EXPRINCDEC:
	case EXPRUNARY:
	case EXPRCAST:
		delexpr(e->base);
		break;
	case EXPRBINARY:
		delexpr(e->u.binary.l);
		delexpr(e->u.binary.r);
		break;
	case EXPRCOND:
		delexpr(e->base);
		if (e->u.cond.t != e->base)
			delexpr(e->u.cond.t);
		delexpr(e->u.cond.f);
		break;
	/*
	XXX: compound assignment causes some reuse of expressions,
	so we can't free them without risk of a double-free

	case EXPRASSIGN:
		delexpr(e->assign.l);
		delexpr(e->assign.r);
		break;
	*/
	case EXPRCOMMA:
		while (sub = e->base) {
			e->base = sub->next;
			delexpr(sub);
		}
		break;
	case EXPRSTMTEXPR: {
		/* items were freed during parsing after funcinit; only
		 * the result expression (if any) needs cleanup here. */
		if (e->u.stmt_expr.last_expr)
			delexpr(e->u.stmt_expr.last_expr);
		break;
	}
	}
	free(e);
}
/* Shallow-structure deep copy of an expression tree, used when a node
 * must be referenced by more than one parent (e.g. the two comparisons
 * in the lowering of `a <=> b`).  delexpr() frees every child it sees,
 * so a node referenced twice would otherwise be freed twice. */
struct expr *
expr_dup(struct expr *e)
{
	struct expr *n, *sub, *prev, *a;
	enum exprkind k;
	(void)sub;
	(void)prev;
	if (!e)
		return NULL;
	n = xmalloc(sizeof *n);
	*n = *e;
	k = e->kind;
	if (k == EXPRCALL) {
		n->base = expr_dup(e->base);
		a = NULL;
		prev = NULL;
		for (sub = e->u.call.args; sub; sub = sub->next) {
			struct expr *c = expr_dup(sub);
			c->next = NULL;
			if (prev)
				prev->next = c;
			else
				a = c;
			prev = c;
		}
		n->u.call.args = a;
	} else if (k == EXPRBITFIELD || k == EXPRINCDEC ||
	    k == EXPRUNARY || k == EXPRCAST) {
		n->base = expr_dup(e->base);
	} else if (k == EXPRBINARY) {
		n->u.binary.l = expr_dup(e->u.binary.l);
		n->u.binary.r = expr_dup(e->u.binary.r);
	} else if (k == EXPRCOND) {
		n->base = expr_dup(e->base);
		n->u.cond.t = expr_dup(e->u.cond.t);
		n->u.cond.f = expr_dup(e->u.cond.f);
	} else if (k == EXPRCOMMA) {
		struct expr *c, *cp, *head;
		head = NULL;
		cp = NULL;
		for (c = e->base; c; c = c->next) {
			struct expr *cc = expr_dup(c);
			cc->next = NULL;
			if (cp)
				cp->next = cc;
			else
				head = cc;
			cp = cc;
		}
		n->base = head;
	}
	return n;
}
struct expr *
mkconstexpr(struct type *t, unsigned long long n)
{
	struct expr *e;

	e = mkexpr(EXPRCONST, t, NULL);
	e->u.constant.u = n;

	return e;
}
struct expr *
decay(struct expr *e)
{
	struct type *t;
	enum typequal tq;

	/* XXX: combine with decl.c:adjust in some way? */
	t = e->type;
	tq = e->qual;
	switch (t->kind) {
	case TYPEARRAY:
		/*
		XXX: qualifiers should be applied to the element
		type of array types, not the array type itself

		assert(tq == QUALNONE);
		*/
		e = mkunaryexpr(TBAND, e);
		e->type = mkpointertype(t->base, t->qual | tq);
		e->decayed = true;
		break;
	case TYPEFUNC:
		e = mkunaryexpr(TBAND, e);
		e->decayed = true;
		break;
	}

	return e;
}
struct expr *
mkunaryexpr(enum tokenkind op, struct expr *base)
{
	struct expr *expr;
	struct type *type;

	switch (op) {
	case TBAND:
		if (base->decayed) {
			expr = base;
			base = base->base;
			free(expr);
		}
		/*
		Allow struct and union types even if they are not lvalues,
		since we take their address when compiling member access.
		*/
		if (!base->lvalue && base->type->kind != TYPEFUNC && base->type->kind != TYPESTRUCT && base->type->kind != TYPEUNION)
			error(&tok.loc, "'&' operand is not an lvalue or function designator");
		if (base->kind == EXPRBITFIELD)
			error(&tok.loc, "cannot take address of bit-field");
		expr = mkexpr(EXPRUNARY, mkpointertype(base->type, base->qual), base);
		expr->op = op;
		return expr;
	case TMUL:
		if (base->type->kind != TYPEPOINTER)
			error_tok_code(E_TYPE, &tok, "cannot dereference non-pointer");
		if (base->kind == EXPRUNARY && base->op == TBAND && base->base->kind != EXPRSTRING) {
			type = base->type->base;
			expr = base->base;
			expr->type = type;
		} else {
			expr = mkexpr(EXPRUNARY, base->type->base, base);
			expr->qual = base->type->qual;
			expr->lvalue = true;
			expr->op = op;
		}
		return decay(expr);
	}
	/* other unary operators get compiled as equivalent binary ones */
	fatal("internal error: unknown unary operator %d", op);
	return NULL;
}
unsigned
bitfieldwidth(struct expr *e)
{
	if (e->kind != EXPRBITFIELD)
		return -1;
	return e->type->size * 8 - e->u.bitfield.bits.before - e->u.bitfield.bits.after;
}
struct expr *
exprconvert(struct expr *e, struct type *t)
{
	if (typecompatible(e->type, t))
		return e;
	return mkexpr(EXPRCAST, t, e);
}
bool
nullpointer(struct expr *e)
{
	if (e->kind != EXPRCONST)
		return false;
	if (e->type->kind == TYPENULLPTR)
		return true;
	if (!(e->type->prop & PROPINT) && (e->type->kind != TYPEPOINTER || e->type->base != &typevoid))
		return false;
	return e->u.constant.u == 0;
}
struct expr *
exprassign(struct expr *e, struct type *t)
{
	struct type *et;

	et = e->type;
	switch (t->kind) {
	case TYPEBOOL:
		if (!(et->prop & PROPARITH) && et->kind != TYPEPOINTER && et->kind != TYPENULLPTR)
			error_tok_code(E_TYPE, &tok, "assignment to bool must be from arithmetic, pointer, or nullptr_t type");
		break;
	case TYPEPOINTER:
		if (nullpointer(e))
			break;
		if (et->kind != TYPEPOINTER)
			error_tok_code(E_TYPE, &tok, "assignment to pointer must be from pointer or null pointer constant");
		if (t->base != &typevoid && et->base != &typevoid && !typecompatible(t->base, et->base)) {
			/* C++: a derived-class pointer converts to a base-class
			 * pointer (upcast).  If the base subobject is not at offset
			 * 0 (multiple inheritance), adjust the address. */
			extern int g_lang;
			extern bool cpp_is_derived(struct type *, struct type *);
			extern unsigned long long cpp_base_offset(struct type *,
			    struct type *);
			if (g_lang == 1 && cpp_is_derived(et->base, t->base)) {
				unsigned long long off = cpp_base_offset(et->base, t->base);
				if (off) {
					struct expr *adj;
					adj = mkbinaryexpr(&tok.loc, TADD,
					    exprconvert(e, &typeulong),
					    mkconstexpr(&typeulong, off));
					adj->type = mkpointertype(t->base, t->qual);
					e = adj;
				}
			} else {
				error_tok_code(E_TYPE, &tok, "base types of pointer assignment must be compatible or void");
			}
		}
		/* void* accepts any qualified pointer (C11 6.3.2.3p1). */
		if (t->base != &typevoid && (et->qual & t->qual) != et->qual)
			error_tok_code(E_TYPE, &tok, "assignment to pointer discards qualifiers");
		break;
	case TYPENULLPTR:
		/* A null pointer constant (incl. the nullptr keyword) or any
		 * expression already of type nullptr_t is assignable. */
		if (!nullpointer(e) && et->kind != TYPENULLPTR)
			error_tok_code(E_TYPE, &tok, "assignment to nullptr_t must be from null pointer constant or expression with type nullptr_t");
		break;
	case TYPESTRUCT:
	case TYPEUNION:
		if (!typecompatible(t, et))
			error_tok_code(E_TYPE, &tok, "assignment to %s type must be from compatible type", t->kind == TYPEUNION ? "union" : "struct");
		break;
	default:
		assert(t->prop & PROPARITH);
		if (!(et->prop & PROPARITH))
			error_tok_code(E_TYPE, &tok, "assignment to arithmetic type must be from arithmetic type");
		break;
	}
	return exprconvert(e, t);
}
struct expr *
exprpromote(struct expr *e)
{
	struct type *t;

	t = typepromote(e->type, bitfieldwidth(e));
	return exprconvert(e, t);
}
struct type *
commonreal(struct expr **e1, struct expr **e2)
{
	struct type *t;

	t = typecommonreal((*e1)->type, bitfieldwidth(*e1), (*e2)->type, bitfieldwidth(*e2));
	*e1 = exprconvert(*e1, t);
	*e2 = exprconvert(*e2, t);

	return t;
}
struct expr *
mksizeofexpr(struct type *t) {
	struct expr *e;

	if (t->kind == TYPEARRAY && t->size == 0) {
		e = mkexpr(EXPRSIZEOF, &typeulong, NULL);
		e->u.szof.type = t;
	} else {
		e = mkconstexpr(&typeulong, t->size);
	}

	return e;
}
struct expr *
mkbinaryexpr(struct location *loc, enum tokenkind op, struct expr *l, struct expr *r)
{
	struct expr *e;
	struct type *t = NULL;
	enum typeprop lp, rp;

	lp = l->type->prop;
	rp = r->type->prop;
	switch (op) {
	case TLOR:
	case TLAND:
		if (!(lp & PROPSCALAR))
			error(loc, "left operand of '%s' operator must be scalar", tokenstr(op));
		if (!(rp & PROPSCALAR))
			error(loc, "right operand of '%s' operator must be scalar", tokenstr(op));
		t = &typeint;
		break;
	case TEQL:
	case TNEQ:
		t = &typeint;
		if (lp & PROPARITH && rp & PROPARITH) {
			commonreal(&l, &r);
			break;
		}
		/* C23: nullptr_t is compatible with any pointer type and
		 * with null pointer constants, so it is a valid operand of
		 * == / != alongside another nullptr_t, a pointer, or 0/nullptr.
		 * We never ask the backend to convert to/from TYPENULLPTR.
		 * In particular, a bare integer null constant (0) is replaced
		 * by a nullptr constant: comparing two TYPENULLPTR values
		 * keeps both operands at the 8-byte pointer size, and it
		 * avoids the branch optimization that would branch on the
		 * nullptr_t value directly (which would require a nullptr_t
		 * -> bool conversion the backend does not implement). */
		if (l->type->kind == TYPENULLPTR || r->type->kind == TYPENULLPTR) {
			struct expr *np, *other;

			if (l->type->kind == TYPENULLPTR) {
				np = l;
				other = r;
			} else {
				np = r;
				other = l;
			}
			if (nullpointer(np) || nullpointer(other)
			    || other->type->kind == TYPEPOINTER
			    || other->type->kind == TYPENULLPTR) {
				if (other->type->kind != TYPEPOINTER
				    && other->type->kind != TYPENULLPTR) {
					other = mkexpr(EXPRCONST, &typenullptr, NULL);
					other->u.constant.u = 0;
				}
				l = np;
				r = other;
				break;
			}
			error(loc, "invalid operands to '%s' operator", tokenstr(op));
		}
		if (l->type->kind != TYPEPOINTER)
			e = l, l = r, r = e;
		if (l->type->kind != TYPEPOINTER)
			error(loc, "invalid operands to '%s' operator", tokenstr(op));
		if (nullpointer(eval(r))) {
			r = exprconvert(r, l->type);
			break;
		}
		if (nullpointer(eval(l))) {
			l = exprconvert(l, r->type);
			break;
		}
		if (r->type->kind != TYPEPOINTER)
			error(loc, "invalid operands to '%s' operator", tokenstr(op));
		if (l->type->base->kind == TYPEVOID)
			e = l, l = r, r = e;
		if (r->type->base->kind == TYPEVOID && l->type->base->kind != TYPEFUNC)
			r = exprconvert(r, l->type);
		else if (!typecompatible(l->type->base, r->type->base))
			error(loc, "pointer operands to '%s' operator are to incompatible types", tokenstr(op));
		break;
	case TLESS:
	case TGREATER:
	case TLEQ:
	case TGEQ:
	case TSPACESHIP:
		t = &typeint;
		if (lp & PROPREAL && rp & PROPREAL) {
			commonreal(&l, &r);
		} else if (l->type->kind == TYPEPOINTER && r->type->kind == TYPEPOINTER) {
			if (!typecompatible(l->type->base, r->type->base) || l->type->base->kind == TYPEFUNC)
				error(loc, "pointer operands to '%s' operator must be to compatible object types", tokenstr(op));
		} else {
			error(loc, "invalid operands to '%s' operator", tokenstr(op));
		}
		break;
	case TBOR:
	case TXOR:
	case TBAND:
		t = commonreal(&l, &r);
		break;
	case TADD:
		if (lp & PROPARITH && rp & PROPARITH) {
			t = commonreal(&l, &r);
			break;
		}
		if (r->type->kind == TYPEPOINTER)
			e = l, l = r, r = e, rp = lp;
		if (l->type->kind != TYPEPOINTER || !(rp & PROPINT))
			error(loc, "invalid operands to '+' operator");
		t = l->type;
		if (t->base->incomplete || t->base->kind == TYPEFUNC)
			error(loc, "pointer operand to '+' must be to complete object type");
		r = mkbinaryexpr(loc, TMUL, exprconvert(r, &typeulong), mksizeofexpr(t->base));
		break;
	case TSUB:
		if (lp & PROPARITH && rp & PROPARITH) {
			t = commonreal(&l, &r);
			break;
		}
		if (l->type->kind != TYPEPOINTER || !(rp & PROPINT) && r->type->kind != TYPEPOINTER)
			error(loc, "invalid operands to '-' operator");
		if (l->type->base->incomplete || l->type->base->kind == TYPEFUNC)
			error(loc, "pointer operand to '-' must be to complete object type");
		if (rp & PROPINT) {
			t = l->type;
			r = mkbinaryexpr(loc, TMUL, exprconvert(r, &typeulong), mksizeofexpr(t->base));
		} else {
			if (!typecompatible(l->type->base, r->type->base))
				error(&tok.loc, "pointer operands to '-' are to incompatible types");
			op = TDIV;
			t = &typelong;
			e = mkbinaryexpr(loc, TSUB, exprconvert(l, &typelong), exprconvert(r, &typelong));
			r = mksizeofexpr(l->type->base);
			l = e;
		}
		break;
	case TMOD:
		if (!(lp & PROPINT) || !(rp & PROPINT))
			error(loc, "operands to '%%' operator must be integer");
		t = commonreal(&l, &r);
		break;
	case TMUL:
	case TDIV:
		if (!(lp & PROPARITH) || !(rp & PROPARITH))
			error(loc, "operands to '%s' operator must be arithmetic", tokenstr(op));
		t = commonreal(&l, &r);
		break;
	case TSHL:
	case TSHR:
		if (!(lp & PROPINT) || !(rp & PROPINT))
			error(loc, "operands to '%s' operator must be integer", tokenstr(op));
		l = exprpromote(l);
		r = exprpromote(r);
		t = l->type;
		break;
	default:
		fatal("internal error: unknown binary operator %d", op);
	}
	if (op == TSPACESHIP) {
		/* C++20 three-way comparison `a <=> b` (simplified int result):
		 *   (a > b) ? 1 : ((a < b) ? -1 : 0)
		 * The two comparisons are EXPRBINARY nodes evaluated for truth
		 * (backend emits a 0/1 int).  They reference the same operands,
		 * so the second comparison uses deep copies (delexpr would
		 * otherwise free the operands twice). */
		struct expr *gt = mkexpr(EXPRBINARY, &typeint, NULL);
		gt->op = TGREATER;
		gt->u.binary.l = l;
		gt->u.binary.r = r;
		struct expr *lt = mkexpr(EXPRBINARY, &typeint, NULL);
		lt->op = TLESS;
		lt->u.binary.l = expr_dup(l);
		lt->u.binary.r = expr_dup(r);
		struct expr *one = mkexpr(EXPRCONST, &typeint, NULL);
		one->u.constant.u = 1;
		struct expr *mone = mkexpr(EXPRCONST, &typeint, NULL);
		mone->u.constant.u = (unsigned long long)-1;
		struct expr *zero = mkexpr(EXPRCONST, &typeint, NULL);
		zero->u.constant.u = 0;
		/* (a < b) ? -1 : 0 */
		struct expr *inner = mkexpr(EXPRCOND, &typeint, lt);
		inner->u.cond.t = mone;
		inner->u.cond.f = zero;
		/* (a > b) ? 1 : inner */
		struct expr *outer = mkexpr(EXPRCOND, &typeint, gt);
		outer->u.cond.t = one;
		outer->u.cond.f = inner;
		return outer;
	}
	e = mkexpr(EXPRBINARY, t, NULL);
	e->op = op;
	e->u.binary.l = l;
	e->u.binary.r = r;

	return e;
}
struct expr *
expr(struct scope *s)
{
	struct expr *r, *e, **end;

	end = &r;
	for (;;) {
		e = assignexpr(s);
		*end = e;
		end = &e->next;
		if (tok.kind != TCOMMA)
			break;
		next();
	}
	if (!r->next)
		return r;
	return mkexpr(EXPRCOMMA, e->type, r);
}
struct expr *
assignexpr(struct scope *s)
{
	struct expr *e, *l, *r, *tmp, *bit;
	enum tokenkind op;

	l = condexpr(s);
	if (l->kind == EXPRBINARY || l->kind == EXPRCOMMA || l->kind == EXPRCAST)
		return l;
	switch (tok.kind) {
	case TASSIGN:     op = TNONE; break;
	case TMULASSIGN:  op = TMUL;  break;
	case TDIVASSIGN:  op = TDIV;  break;
	case TMODASSIGN:  op = TMOD;  break;
	case TADDASSIGN:  op = TADD;  break;
	case TSUBASSIGN:  op = TSUB;  break;
	case TSHLASSIGN:  op = TSHL;  break;
	case TSHRASSIGN:  op = TSHR;  break;
	case TBANDASSIGN: op = TBAND; break;
	case TXORASSIGN:  op = TXOR;  break;
	case TBORASSIGN:  op = TBOR;  break;
	default:
		return l;
	}
	if (!l->lvalue)
		error(&tok.loc, "left side of assignment expression is not an lvalue");
	next();
	r = assignexpr(s);
	if (!op)
		return mkassignexpr(l, r);
	/* Do not expand atomic +=/-= into a load/op/store sequence: that loses
	 * updates under contention.  Preserve the one-evaluation guarantee by
	 * passing the lvalue's address directly to IR generation. */
	if ((l->qual & QUALATOMIC) && (op == TADD || op == TSUB)) {
		e = mkexpr(EXPRBUILTIN, l->type, mkunaryexpr(TBAND, l));
		e->base->next = exprassign(r, l->type);
		e->u.builtin.kind = op == TADD ? BUILTINATOMICADDASSIGN : BUILTINATOMICSUBASSIGN;
		return e;
	}
	/* rewrite `E1 OP= E2` as `T = &E1, *T = *T OP E2`, where T is a temporary slot */
	if (l->kind == EXPRBITFIELD) {
		bit = l;
		l = l->base;
	} else {
		bit = NULL;
	}
	tmp = mkexpr(EXPRTEMP, mkpointertype(l->type, l->qual), NULL);
	tmp->lvalue = true;
	tmp->u.temp = NULL;
	e = mkassignexpr(tmp, mkunaryexpr(TBAND, l));
	l = mkunaryexpr(TMUL, tmp);
	if (bit) {
		bit->base = l;
		l = bit;
	}
	r = mkbinaryexpr(&tok.loc, op, l, r);
	e->next = mkassignexpr(l, r);
	return mkexpr(EXPRCOMMA, l->type, e);
}
struct expr *
mkassignexpr(struct expr *l, struct expr *r)
{
	struct expr *e;

	e = mkexpr(EXPRASSIGN, l->type, NULL);
	e->u.assign.l = l;
	e->u.assign.r = exprassign(r, l->type);
	return e;
}
