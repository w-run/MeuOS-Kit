#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include "util.h"
#include "mcc.h"

enum {
	F = 1<<8,
	S = 2<<8
};

static void
cast(struct expr *expr)
{
	struct type *t;
	unsigned long long m;

	t = expr->type;
	if (t->prop & PROPFLOAT) {
		if (t->size == 4)
			expr->u.constant.f = (float)expr->u.constant.f;
	} else if (t->prop & PROPINT) {
		assert(t->u.arith.width > 0);
		m = 1ull << t->u.arith.width - 1;
		expr->u.constant.u &= m | m - 1;
		if (t->u.arith.issigned)
			expr->u.constant.u = (expr->u.constant.u ^ m) - m;
	}
}

static void
unary(struct expr *expr, enum tokenkind op, struct expr *l)
{
	expr->kind = EXPRCONST;
	if (l->type->prop & PROPFLOAT)
		op |= F;
	switch (op) {
	case TSUB:      expr->u.constant.u = -l->u.constant.u; break;
	case TSUB|F:    expr->u.constant.f = -l->u.constant.f; break;
	default:
		fatal("internal error; unknown unary expression");
	}
	cast(expr);
}

static void
binary(struct expr *expr, enum tokenkind op, struct expr *l, struct expr *r)
{
	expr->kind = EXPRCONST;
	if (l->type->prop & PROPFLOAT)
		op |= F;
	else if (l->type->prop & PROPINT && l->type->u.arith.issigned)
		op |= S;
	switch (op) {
	case TMUL:
	case TMUL|S:     expr->u.constant.u = l->u.constant.u * r->u.constant.u; break;
	case TMUL|F:     expr->u.constant.f = l->u.constant.f * r->u.constant.f; break;
	case TDIV:       expr->u.constant.u = l->u.constant.u / r->u.constant.u; break;
	case TDIV|S:     expr->u.constant.i = l->u.constant.i / r->u.constant.i; break;
	case TDIV|F:     expr->u.constant.f = l->u.constant.f / r->u.constant.f; break;
	case TMOD:       expr->u.constant.u = l->u.constant.u % r->u.constant.u; break;
	case TMOD|S:     expr->u.constant.i = l->u.constant.i % r->u.constant.i; break;
	case TADD:
	case TADD|S:     expr->u.constant.u = l->u.constant.u + r->u.constant.u; break;
	case TADD|F:     expr->u.constant.f = l->u.constant.f + r->u.constant.f; break;
	case TSUB:
	case TSUB|S:     expr->u.constant.u = l->u.constant.u - r->u.constant.u; break;
	case TSUB|F:     expr->u.constant.f = l->u.constant.f - r->u.constant.f; break;
	case TSHL:
	case TSHL|S:     expr->u.constant.u = l->u.constant.u << (r->u.constant.u & 63); break;
	case TSHR:       expr->u.constant.u = l->u.constant.u >> (r->u.constant.u & 63); break;
	case TSHR|S:     expr->u.constant.i = l->u.constant.i >> (r->u.constant.u & 63); break;
	case TBAND:
	case TBAND|S:    expr->u.constant.u = l->u.constant.u & r->u.constant.u; break;
	case TBOR:
	case TBOR|S:     expr->u.constant.u = l->u.constant.u | r->u.constant.u; break;
	case TXOR:
	case TXOR|S:     expr->u.constant.u = l->u.constant.u ^ r->u.constant.u; break;
	case TLESS:      expr->u.constant.u = l->u.constant.u < r->u.constant.u; break;
	case TLESS|S:    expr->u.constant.u = l->u.constant.i < r->u.constant.i; break;
	case TLESS|F:    expr->u.constant.u = l->u.constant.f < r->u.constant.f; break;
	case TGREATER:   expr->u.constant.u = l->u.constant.u > r->u.constant.u; break;
	case TGREATER|S: expr->u.constant.u = l->u.constant.i > r->u.constant.i; break;
	case TGREATER|F: expr->u.constant.u = l->u.constant.f > r->u.constant.f; break;
	case TLEQ:       expr->u.constant.u = l->u.constant.u <= r->u.constant.u; break;
	case TLEQ|S:     expr->u.constant.u = l->u.constant.i <= r->u.constant.i; break;
	case TLEQ|F:     expr->u.constant.u = l->u.constant.f <= r->u.constant.f; break;
	case TGEQ:       expr->u.constant.u = l->u.constant.u >= r->u.constant.u; break;
	case TGEQ|S:     expr->u.constant.u = l->u.constant.i >= r->u.constant.i; break;
	case TGEQ|F:     expr->u.constant.u = l->u.constant.f >= r->u.constant.f; break;
	case TEQL:
	case TEQL|S:     expr->u.constant.u = l->u.constant.u == r->u.constant.u; break;
	case TEQL|F:     expr->u.constant.u = l->u.constant.f == r->u.constant.f; break;
	case TNEQ:
	case TNEQ|S:     expr->u.constant.u = l->u.constant.u != r->u.constant.u; break;
	case TNEQ|F:     expr->u.constant.u = l->u.constant.f != r->u.constant.f; break;
	default:
		fatal("internal error; unknown binary expression");
	}
	cast(expr);
}

struct expr *
eval(struct expr *expr)
{
	struct expr *l, *r, *c;
	struct decl *d;
	struct type *t;

	t = expr->type;
	switch (expr->kind) {
	case EXPRIDENT:
		d = expr->u.ident.decl;
		if (d->kind != DECLCONST) {
			/* C++ constexpr variable or C23 constexpr object: its
			 * (integer) value is usable in later constant expressions
			 * (e.g. _Static_assert, array dimensions). */
			extern int g_lang;
			if (d->kind == DECLOBJECT && d->u.obj.has_constval &&
			    (d->type->prop & PROPINT) &&
			    (g_lang == 1 || (d->qual & QUALCONSTEXPR))) {
				expr->kind = EXPRCONST;
				expr->u.constant.u = d->u.obj.constval;
			}
			break;
		}
		expr->kind = EXPRCONST;
		expr->u.constant.u = d->u.enumconst;
		break;
	case EXPRCOMPOUND:
		d = expr->u.compound.decl;
		if (d->u.obj.storage != SDSTATIC)
			break;
		d->value = mkglobal(d);
		emitdata(d, expr->u.compound.init);
		expr->kind = EXPRIDENT;
		expr->u.ident.decl = d;
		break;
	case EXPRUNARY:
		l = eval(expr->base);
		switch (expr->op) {
		case TBAND:
			switch (l->kind) {
			case EXPRUNARY:
				if (l->op == TMUL)
					expr = eval(l->base);
				break;
			case EXPRSTRING:
				l->u.ident.decl = stringdecl(l);
				l->kind = EXPRIDENT;
				expr->base = l;
				break;
			}
			break;
		case TMUL:
		/* C++ constexpr aggregate object / call-return value: fold a member
		 * access.  An lvalue object `p.a` lowers to `*(&p + offset)`
		 * (`&p` = EXPRUNARY TBAND of EXPRIDENT); an rvalue call result
		 * `make_p(3).a` lowers to `*(make_p(3) + offset)` (the call is
		 * the object address directly).  Both fold through the mini
		 * memory model (member value tables). */
		{
			extern int g_lang;
			struct expr *b = expr->base;
			struct expr *objexpr = NULL;
			unsigned long long off = 0;
			if (g_lang == 1 && b && b->kind == EXPRBINARY &&
			    b->op == TADD && b->u.binary.l && b->u.binary.r &&
			    b->u.binary.r->kind == EXPRCONST) {
				struct expr *l = b->u.binary.l;
				if (l->kind == EXPRUNARY && l->op == TBAND &&
				    l->base) {
					/* `&p` or `&make_p(3)` (rvalue call
					 * materialized as an address) */
					if (l->base->kind == EXPRIDENT)
						objexpr = l->base;
					else if (l->base->kind == EXPRCALL)
						objexpr = l->base;
				} else if (l->kind == EXPRCALL) {
					objexpr = l; /* make_p(3) */
				}
				off = b->u.binary.r->u.constant.u;
			}
			if (objexpr && objexpr->kind == EXPRIDENT) {
				struct decl *obj = objexpr->u.ident.decl;
				if (obj && obj->kind == DECLOBJECT &&
				    obj->type && (obj->type->kind == TYPESTRUCT ||
				    obj->type->kind == TYPEUNION)) {
					unsigned long long mv;
					extern bool cpp_cexpr_member_value(
					    struct decl *, unsigned long long,
					    unsigned long long *);
					if (cpp_cexpr_member_value(obj, off, &mv)) {
						expr->kind = EXPRCONST;
						expr->u.constant.u = mv;
						break;
					}
					/* first member at offset 0: the object's
					 * whole-value constval */
					if (off == 0 && obj->u.obj.has_constval) {
						expr->kind = EXPRCONST;
						expr->u.constant.u =
						    obj->u.obj.constval;
						break;
					}
				}
			} else if (objexpr && objexpr->kind == EXPRCALL) {
				/* member access on a constexpr call's class return
				 * value: fold the call (recording its return
				 * members), then read the member at `off`. */
				struct expr *call = objexpr;
				unsigned long long mv;
				extern struct expr *cpp_constexpr_eval(
				    struct expr *);
				extern bool cpp_cexpr_ret_member_value(
				    struct expr *, unsigned long long,
				    unsigned long long *);
				struct expr *rv = cpp_constexpr_eval(call);
				if (rv)
					delexpr(rv);
				if (cpp_cexpr_ret_member_value(call, off, &mv)) {
					expr->kind = EXPRCONST;
					expr->u.constant.u = mv;
					break;
				}
			}
		}
		break;
	default:
		if (l->kind != EXPRCONST)
			break;
		unary(expr, expr->op, l);
		break;
	}
	break;
	case EXPRCAST:
		l = eval(expr->base);
		if (l->kind == EXPRCONST) {
			expr->kind = EXPRCONST;
			if (l->type->prop & PROPINT && t->prop & PROPFLOAT) {
				if (l->type->u.arith.issigned)
					expr->u.constant.f = l->u.constant.i;
				else
					expr->u.constant.f = l->u.constant.u;
			} else if (l->type->prop & PROPFLOAT && t->prop & PROPINT) {
				if (t->u.arith.issigned) {
					if (l->u.constant.f < -0x1p63 || l->u.constant.f >= 0x1p63)
						error(&tok.loc, "integer part of floating-point constant %g cannot be represented as signed integer", l->u.constant.f);
					expr->u.constant.i = l->u.constant.f;
				} else {
					if (l->u.constant.f < 0.0 || l->u.constant.f >= 0x1p64)
						error(&tok.loc, "integer part of floating-point constant %g cannot be represented as unsigned integer", l->u.constant.f);
					expr->u.constant.u = l->u.constant.f;
				}
			} else {
				expr->u.constant = l->u.constant;
			}
			cast(expr);
		} else if (l->type->kind == TYPEPOINTER) {
			/*
			A cast from a pointer to integer is not a valid constant
			expression, but C11 allows implementations to recognize
			other forms of constant expressions (6.6p10), and some
			programs expect this functionality.
			*/
			if (t->kind == TYPEPOINTER || t->prop & PROPINT && t->size == typelong.size)
				expr = l;
		}
		break;
	case EXPRBINARY:
		l = eval(expr->u.binary.l);
		r = eval(expr->u.binary.r);
		expr->u.binary.l = l;
		expr->u.binary.r = r;
		switch (expr->op) {
		case TADD:
			if (r->kind == EXPRBINARY)
				c = l, l = r, r = c;
			/* fallthrough */
		case TSUB:
			if (r->kind != EXPRCONST)
				break;
			if (l->kind == EXPRCONST) {
				binary(expr, expr->op, l, r);
			} else if (l->kind == EXPRBINARY && l->type->kind == TYPEPOINTER && l->op == TADD && l->u.binary.r->kind == EXPRCONST) {
				/* (P + C1) ± C2  ->  P + (C1 ± C2) */
				binary(expr->u.binary.r, expr->op, l->u.binary.r, r);
				expr->op = TADD;
				expr->u.binary.l = l->u.binary.l;
			}
			break;
		case TLOR:
			if (l->kind != EXPRCONST)
				break;
			return l->u.constant.u ? l : r;
		case TLAND:
			if (l->kind != EXPRCONST)
				break;
			return l->u.constant.u ? r : l;
		default:
			if (l->kind != EXPRCONST || r->kind != EXPRCONST)
				break;
			binary(expr, expr->op, l, r);
		}
		break;
	case EXPRCALL:
		/* constexpr function (C23, and C++): fold the call when the
		 * callee is a constexpr function and all arguments are integer
		 * constants. */
		{
			extern struct expr *cpp_constexpr_eval(struct expr *);
			struct expr *r = cpp_constexpr_eval(expr);
			if (r)
				return r;
		}
		break;
	}

	return expr;
}
