/* branch.c — Lvalue evaluation and short-circuit branches.
 *
 * `funclval` lowers a C lvalue expression (identifier, string literal,
 * compound literal, dereferenced pointer, or struct/union rvalue) into
 * an address-bearing `struct lvalue` for use by funcload/funcstore. It
 * also lazily emits the `__func__` static string the first time the
 * implicit `__func__` identifier is referenced in a function.
 *
 * `funcbranch` short-circuits boolean conditions: `||` and `&&` lower
 * to nested funcbranch calls sharing an intermediate block; `== 0` /
 * `!= 0` flips branch targets rather than emitting a comparison; comma
 * expressions evaluate each operand and forward the last to funcbranch.
 * Falls back to funcjnz for non-specializable conditions. */
#include "irgen.h"

struct lvalue
funclval(struct func *f, struct expr *e)
{
	struct lvalue lval = {0};
	struct decl *d;

	if (e->kind == EXPRBITFIELD) {
		lval.bits = e->u.bitfield.bits;
		e = e->base;
	}
	switch (e->kind) {
	case EXPRIDENT:
		d = e->u.ident.decl;
		if (d->kind != DECLOBJECT && d->kind != DECLFUNC)
			error(&tok.loc, "identifier '%s' is not an object or function", d->name);
		if (d == f->namedecl) {
			fputs("data ", stdout);
			emitname(d->value);
			printf(" = { b \"%s\", b 0 }\n", f->name);
			f->namedecl = NULL;
		}
		lval.addr = d->value;
		break;
	case EXPRSTRING:
		d = stringdecl(e);
		lval.addr = d->value;
		break;
	case EXPRCOMPOUND:
		if (e->toeval)
			funcexpr(f, e->toeval);
		d = e->u.compound.decl;
		funcinit(f, d, e->u.compound.init, true);
		lval.addr = d->value;
		break;
	case EXPRUNARY:
		if (e->op != TMUL)
			error(&tok.loc, "expression is not an object");
		lval.addr = funcexpr(f, e->base);
		break;
	default:
		if (e->type->kind != TYPESTRUCT && e->type->kind != TYPEUNION)
			error(&tok.loc, "expression is not an object");
		lval.addr = funcexpr(f, e);
	}
	return lval;
}

/* returns the expression value in the true branch */
struct value *
funcbranch(struct func *f, struct expr *e, struct block *bt, struct block *bf)
{
	static struct value one = {.kind = VALUE_INTCONST, .u.i = 1};
	struct expr *l, *r;
	struct value *v;
	struct block *b;

	/* Maybe we we could do something for EXPRCOND as well. */
	switch (e->kind) {
	case EXPRBINARY:
		l = e->u.binary.l;
		r = e->u.binary.r;
		switch (e->op) {
		case TEQL:
		case TNEQ:
			r = eval(r);
			if (r->kind == EXPRCONST && r->type->prop & PROPINT && r->u.constant.u == 0) {
				if (e->op == TEQL)
					b = bt, bt = bf, bf = b;
				funcbranch(f, l, bt, bf);
				return &one;
			}
			break;
		case TLOR:
		case TLAND:
			if (e->op == TLOR) {
				b = mkblock("logic_or");
				funcbranch(f, l, bt, b);
			} else {
				b = mkblock("logic_and");
				funcbranch(f, l, b, bf);
			}
			funclabel(f, b);
			funcbranch(f, r, bt, bf);
			return &one;
		}
		break;
	case EXPRCOMMA:
		for (e = e->base; e->next; e = e->next)
			funcexpr(f, e);
		return funcbranch(f, e, bt, bf);
	}
	v = funcexpr(f, e);
	funcjnz(f, v, e->type, bt, bf);
	return v;
}
