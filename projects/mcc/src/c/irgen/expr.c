/* expr.c — C expression → IR translation, plus local initializer lowering.
 *
 * `funcexpr` is the workhorse: it walks every flavor of `struct expr`
 * (identifier / constant / bitfield / compound / inc-dec / call / unary /
 * cast / binary / conditional / assign / comma / builtin / temp / sizeof)
 * and emits the corresponding IR via `funcinst` / `funcstore` / `funcload`.
 * Short-circuit logical operators (`||`, `&&`) and the conditional
 * operator (`?:`) build CFG diamonds with binary phi nodes at the join.
 *
 * `zero` and `funcinit` lower local initializers: gap-filling zero
 * stores, scalar stores (with bitfield insertion), and string-literal
 * element-wise stores. Phase 1a exercises only the trivial paths. */
#include "irgen.h"

/* The compiler-runtime ABI returns sub-word integers in a general-purpose
 * register.  Normalize them here just as ILOADSB/ILOADSH would, otherwise
 * an atomic signed char/short with its high bit set compares incorrectly. */
static struct value *
atomicresult(struct func *f, struct type *t, struct value *v)
{
	if (!(t->prop & PROPINT) || t->size >= 4)
		return v;
	switch (t->size) {
	case 1: return funcinst(f, t->u.arith.issigned ? IEXTSB : IEXTUB, 'w', v, NULL);
	case 2: return funcinst(f, t->u.arith.issigned ? IEXTSH : IEXTUH, 'w', v, NULL);
	default: return v;
	}
}

/* The compiler ABI follows libatomic's width-specific fetch-add/sub entry
 * points.  They are sequentially consistent, so callers never get a weaker
 * ordering than requested by the C11 interface.  This is intentionally a
 * call rather than a load/add/store expansion: the latter loses updates when
 * two threads race.  The driver supplies -latomic for host links; MeuOS libc
 * will provide the same ABI as part of its compiler runtime. */
static struct value *
atomicrmw(struct func *f, enum builtinkind kind, struct type *t,
          struct lvalue lval, struct value *arg)
{
	static struct decl callee[6] = {0};
	struct decl *d;
	char *name;
	struct value *v;
	struct irtype qt;

	if (!(t->prop & PROPINT) || (t->size != 1 && t->size != 2 &&
	    t->size != 4 && t->size != 8))
		error(&tok.loc, "atomic fetch operation currently requires an integer type up to 64 bits");
	const char *opname;
	int slot;

	switch (kind) {
	case BUILTINATOMICFETCHADD: case BUILTINATOMICADDASSIGN: opname = "add"; slot = 0; break;
	case BUILTINATOMICFETCHSUB: case BUILTINATOMICSUBASSIGN: opname = "sub"; slot = 1; break;
	case BUILTINATOMICFETCHAND: opname = "and"; slot = 2; break;
	case BUILTINATOMICFETCHOR:  opname = "or";  slot = 3; break;
	case BUILTINATOMICFETCHXOR: opname = "xor"; slot = 4; break;
	case BUILTINATOMICEXCHANGE: opname = "exchange"; slot = 5; break;
	default: fatal("internal error: invalid atomic RMW builtin");
	}
	d = &callee[slot];
	name = strf(PHeap, "__atomic_fetch_%s_%llu",
	            opname, t->size);
	if (kind == BUILTINATOMICEXCHANGE)
		name = strf(PHeap, "__atomic_exchange_%llu", t->size);
	d->name = name;
	d->kind = DECLFUNC;
	d->linkage = LINKEXTERN;
	qt = irtype(t);
	v = funcinst(f, ICALL, qt.base, mkglobal(d), NULL);
	funcinst(f, IARG, ptrclass(), lval.addr, NULL);
	funcinst(f, IARG, qt.base, arg, NULL);
	funcinst(f, IARG, 'w', mkintconst(5), NULL); /* __ATOMIC_SEQ_CST */
	return atomicresult(f, t, v);
}

static struct value *
atomiccompareexchange(struct func *f, struct type *t, struct value *object,
                      struct value *expected, struct value *desired)
{
	static struct decl callee;
	struct irtype qt;
	struct value *v;

	if (!(t->prop & PROPSCALAR) || t->size > 8)
		error(&tok.loc, "atomic compare exchange currently requires a scalar type up to 64 bits");
	qt = irtype(t);
	callee.name = strf(PHeap, "__atomic_compare_exchange_%llu", t->size);
	callee.kind = DECLFUNC;
	callee.linkage = LINKEXTERN;
	v = funcinst(f, ICALL, 'w', mkglobal(&callee), NULL);
	funcinst(f, IARG, ptrclass(), object, NULL);
	funcinst(f, IARG, ptrclass(), expected, NULL);
	funcinst(f, IARG, qt.base, desired, NULL);
	funcinst(f, IARG, 'w', mkintconst(0), NULL); /* strong */
	funcinst(f, IARG, 'w', mkintconst(5), NULL); /* success: seq_cst */
	funcinst(f, IARG, 'w', mkintconst(5), NULL); /* failure: seq_cst */
	return atomicresult(f, t, v);
}

static struct value *
atomicload(struct func *f, struct type *t, struct value *addr)
{
	static struct decl callee;
	struct irtype qt;
	struct value *v;

	if (!(t->prop & PROPSCALAR) || t->size > 8)
		error(&tok.loc, "atomic load currently requires a scalar type up to 64 bits");
	qt = irtype(t);
	callee.name = strf(PHeap, "__atomic_load_%llu", t->size);
	callee.kind = DECLFUNC;
	callee.linkage = LINKEXTERN;
	v = funcinst(f, ICALL, qt.base, mkglobal(&callee), NULL);
	funcinst(f, IARG, ptrclass(), addr, NULL);
	funcinst(f, IARG, 'w', mkintconst(5), NULL); /* __ATOMIC_SEQ_CST */
	return atomicresult(f, t, v);
}

static void
atomicstore(struct func *f, struct type *t, struct value *addr, struct value *value)
{
	static struct decl callee;
	struct irtype qt;

	if (!(t->prop & PROPSCALAR) || t->size > 8)
		error(&tok.loc, "atomic store currently requires a scalar type up to 64 bits");
	qt = irtype(t);
	callee.name = strf(PHeap, "__atomic_store_%llu", t->size);
	callee.kind = DECLFUNC;
	callee.linkage = LINKEXTERN;
	funcinst(f, ICALL, 0, mkglobal(&callee), NULL);
	funcinst(f, IARG, ptrclass(), addr, NULL);
	funcinst(f, IARG, qt.base, value, NULL);
	funcinst(f, IARG, 'w', mkintconst(5), NULL); /* __ATOMIC_SEQ_CST */
}

struct value *
funcexpr(struct func *f, struct expr *e)
{
	enum instkind op = INONE;
	struct decl *d;
	struct value *l, *r, *v, **argvals;
	struct lvalue lval;
	struct expr *arg;
	struct block *b[3];
	struct type *t, *functype;
	size_t i;

	if (e->type->prop & PROPVM)
		calcvla(f, e->type);
	switch (e->kind) {
	case EXPRIDENT:
		d = e->u.ident.decl;
		switch (d->kind) {
		case DECLOBJECT:
			if (e->qual & QUALATOMIC)
				return atomicload(f, e->type, d->value);
			return funcload(f, e->type, e->qual, (struct lvalue){d->value});
		case DECLCONST:  return d->value;
		default:
			fatal("unimplemented declaration kind %d", d->kind);
		}
		break;
	case EXPRCONST:
		t = e->type;
		if (t->prop & PROPINT || t->kind == TYPEPOINTER || t->kind == TYPENULLPTR)
			return mkintconst(e->u.constant.u);
		assert(t->prop & PROPFLOAT);
		return mkfltconst(t->size == 4 ? VALUE_FLTCONST : VALUE_DBLCONST, e->u.constant.f);
	case EXPRBITFIELD:
	case EXPRCOMPOUND:
		lval = funclval(f, e);
		return funcload(f, e->type, e->qual, lval);
	case EXPRINCDEC:
		lval = funclval(f, e->base);
		/* C11 requires ++/-- applied to an atomic object to be a single
		 * read-modify-write operation. */
		if (e->base->qual & QUALATOMIC) {
			t = e->type;
			if (t->kind == TYPEPOINTER || !(t->prop & PROPINT))
				error(&tok.loc, "atomic increment/decrement currently requires an integer type");
			r = mkintconst(1);
			l = atomicrmw(f, e->op == TINC ? BUILTINATOMICFETCHADD : BUILTINATOMICFETCHSUB,
			              t, lval, r);
			if (e->u.incdec.post)
				return l;
			return funcinst(f, e->op == TINC ? IADD : ISUB, irtype(t).base, l, r);
		}
		l = funcload(f, e->base->type, e->qual, lval);
		t = e->type;
		if (t->kind == TYPEPOINTER) {
			if (t->base->kind == TYPEARRAY && t->base->size == 0) {
				r = t->base->u.array.size;
			} else {
				r = mkintconst(t->base->size);
			}
		} else if (t->prop & PROPINT) {
			r = mkintconst(1);
		} else if (t->prop & PROPFLOAT) {
			r = mkfltconst(t->size == 4 ? VALUE_FLTCONST : VALUE_DBLCONST, 1);
		} else {
			fatal("not a scalar");
		}
		v = funcinst(f, e->op == TINC ? IADD : ISUB, irtype(t).base, l, r);
		v = funcstore(f, e->type, e->qual, lval, v);
		return e->u.incdec.post ? l : v;
	case EXPRCALL:
		/* C++ template instantiation (D2): a member of an instantiated
		 * class template whose body was deferred must be parsed before its
		 * first call, or the callee symbol has no emitted body. */
		{
			extern int g_lang;
			struct expr *cal = e->base;
			if (g_lang == 1 && cal &&
			    cal->kind == EXPRUNARY && cal->op == TBAND)
				cal = cal->base;
			if (g_lang == 1 && cal && cal->kind == EXPRIDENT &&
			    cal->u.ident.decl &&
			    cal->u.ident.decl->kind == DECLFUNC &&
			    !cal->u.ident.decl->defined) {
				extern bool cpp_ensure_method_defined(struct decl *);
				cpp_ensure_method_defined(cal->u.ident.decl);
			}
		}
		argvals = xreallocarray(NULL, e->u.call.nargs, sizeof(argvals[0]));
		for (arg = e->u.call.args, i = 0; arg; arg = arg->next, ++i) {
			emittype(arg->type);
			argvals[i] = funcexpr(f, arg);
		}
		t = e->type;
		emittype(t);
		v = funcinst(f, ICALL, irtype(t).base, funcexpr(f, e->base), t->value);
		functype = e->base->type->base;
		for (arg = e->u.call.args, i = 0; arg; arg = arg->next, ++i) {
			if (functype->u.func.isvararg && i == functype->u.func.nparam)
				funcinst(f, IVARARG, 0, NULL, NULL);
			t = arg->type;
			funcinst(f, IARG, irtype(t).base, argvals[i], t->value);
		}
		e = e->base;
		if (e->kind == EXPRUNARY && e->op == TBAND) {
			e = e->base;
			if (e->kind == EXPRIDENT && e->u.ident.decl->u.func.isnoreturn)
				funchlt(f);
		}
		return v;
	case EXPRUNARY:
		switch (e->op) {
		case TBAND:
			lval = funclval(f, e->base);
			return lval.addr;
		case TMUL:
			r = funcexpr(f, e->base);
			if (e->qual & QUALATOMIC)
				return atomicload(f, e->type, r);
			return funcload(f, e->type, e->qual, (struct lvalue){r});
		case TSUB:
			r = funcexpr(f, e->base);
			return funcinst(f, INEG, irtype(e->type).base, r, NULL);
		case T__REAL__:
			/* __real__ complex_value — load first sizeof(base) bytes */
			lval = funclval(f, e->base);
			return funcload(f, e->type, e->qual, lval);
		case T__IMAG__:
			/* __imag__ complex_value — load second sizeof(base) bytes */
			lval = funclval(f, e->base);
			{
				struct value *addr = lval.addr;
				addr = funcinst(f, IADD, ptrclass(), addr, mkintconst(e->type->size));
				return funcload(f, e->type, e->qual, (struct lvalue){addr});
			}
		}
		fatal("internal error; unknown unary expression");
		break;
	case EXPRCAST:
		if (e->toeval)
			funcexpr(f, e->toeval);
		l = funcexpr(f, e->base);
		return convert(f, e->type, e->base->type, l);
	case EXPRBINARY:
		if (e->op == TLOR || e->op == TLAND) {
			b[0] = mkblock("logic_true");
			b[1] = mkblock("logic_false");
			b[2] = mkblock("logic_join");

			funcbranch(f, e, b[0], b[1]);
			funclabel(f, b[0]);
			funcjmp(f, b[2]);
			funclabel(f, b[1]);

			b[2]->phi.class = 'w';
			b[2]->phi.blk[0] = b[0];
			b[2]->phi.val[0] = mkintconst(1);
			b[2]->phi.blk[1] = b[1];
			b[2]->phi.val[1] = mkintconst(0);
			functemp(f, &b[2]->phi.res);
			funclabel(f, b[2]);

			return &b[2]->phi.res;
		}
		l = funcexpr(f, e->u.binary.l);
		r = funcexpr(f, e->u.binary.r);
		t = e->u.binary.l->type;
		if (t->kind == TYPEPOINTER)
			t = &typeulong;
		switch (e->op) {
		case TMUL:
			op = IMUL;
			break;
		case TDIV:
			op = !(t->prop & PROPINT) || t->u.arith.issigned ? IDIV : IUDIV;
			break;
		case TMOD:
			op = t->u.arith.issigned ? IREM : IUREM;
			break;
		case TADD:
			op = IADD;
			break;
		case TSUB:
			op = ISUB;
			break;
		case TSHL:
			op = ISHL;
			break;
		case TSHR:
			op = t->u.arith.issigned ? ISAR : ISHR;
			break;
		case TBOR:
			op = IOR;
			break;
		case TBAND:
			op = IAND;
			break;
		case TXOR:
			op = IXOR;
			break;
		case TLESS:
			if (t->size <= 4)
				op = t->prop & PROPFLOAT ? ICLTS : t->u.arith.issigned ? ICSLTW : ICULTW;
			else
				op = t->prop & PROPFLOAT ? ICLTD : t->u.arith.issigned ? ICSLTL : ICULTL;
			break;
		case TGREATER:
			if (t->size <= 4)
				op = t->prop & PROPFLOAT ? ICGTS : t->u.arith.issigned ? ICSGTW : ICUGTW;
			else
				op = t->prop & PROPFLOAT ? ICGTD : t->u.arith.issigned ? ICSGTL : ICUGTL;
			break;
		case TLEQ:
			if (t->size <= 4)
				op = t->prop & PROPFLOAT ? ICLES : t->u.arith.issigned ? ICSLEW : ICULEW;
			else
				op = t->prop & PROPFLOAT ? ICLED : t->u.arith.issigned ? ICSLEL : ICULEL;
			break;
		case TGEQ:
			if (t->size <= 4)
				op = t->prop & PROPFLOAT ? ICGES : t->u.arith.issigned ? ICSGEW : ICUGEW;
			else
				op = t->prop & PROPFLOAT ? ICGED : t->u.arith.issigned ? ICSGEL : ICUGEL;
			break;
		case TEQL:
			if (t->size <= 4)
				op = t->prop & PROPFLOAT ? ICEQS : ICEQW;
			else
				op = t->prop & PROPFLOAT ? ICEQD : ICEQL;
			break;
		case TNEQ:
			if (t->size <= 4)
				op = t->prop & PROPFLOAT ? ICNES : ICNEW;
			else
				op = t->prop & PROPFLOAT ? ICNED : ICNEL;
			break;
		}
		if (op == INONE)
			fatal("internal error; unimplemented binary expression");
		v = funcinst(f, op, irtype(e->type).base, l, r);
		if (t->kind == TYPEBITINT)
			v = funcbits(f, t, v, (struct bitfield){0});
		return v;
	case EXPRCOND:
		b[0] = mkblock("cond_true");
		b[1] = mkblock("cond_false");
		b[2] = mkblock("cond_join");

		v = funcbranch(f, e->base, b[0], b[1]);

		funclabel(f, b[0]);
		if (e->u.cond.t != e->base)
			v = funcexpr(f, e->u.cond.t);
		b[2]->phi.val[0] = convert(f, e->type, e->u.cond.t->type, v);
		b[2]->phi.blk[0] = f->end;
		funcjmp(f, b[2]);

		funclabel(f, b[1]);
		v = funcexpr(f, e->u.cond.f);
		b[2]->phi.val[1] = convert(f, e->type, e->u.cond.f->type, v);
		b[2]->phi.blk[1] = f->end;

		funclabel(f, b[2]);
		if (e->type == &typevoid)
			return NULL;
		functemp(f, &b[2]->phi.res);
		b[2]->phi.class = irtype(e->type).base;
		return &b[2]->phi.res;
	case EXPRASSIGN:
		r = funcexpr(f, e->u.assign.r);
		if (e->u.assign.l->kind == EXPRTEMP) {
			e->u.assign.l->u.temp = r;
		} else {
			lval = funclval(f, e->u.assign.l);
			if (e->u.assign.l->qual & QUALATOMIC)
				atomicstore(f, e->u.assign.l->type, lval.addr, r);
			else
				r = funcstore(f, e->u.assign.l->type, e->u.assign.l->qual, lval, r);
		}
		return r;
	case EXPRCOMMA:
		for (e = e->base; e->next; e = e->next)
			funcexpr(f, e);
		return funcexpr(f, e);
	case EXPRSTMTEXPR: {
		/* GNU statement expression ({...}).
		 * All declarations, side-effect expressions, and control-flow
		 * statements were emitted to curfunc during parsing.  Here we
		 * only evaluate the result expression (if any). */
		if (e->u.stmt_expr.last_expr)
			return funcexpr(f, e->u.stmt_expr.last_expr);
		return NULL;
	}
	case EXPRBUILTIN:
		switch (e->u.builtin.kind) {
		case BUILTINATOMICFETCHADD:
		case BUILTINATOMICFETCHSUB:
		case BUILTINATOMICFETCHAND:
		case BUILTINATOMICFETCHOR:
		case BUILTINATOMICFETCHXOR:
		case BUILTINATOMICEXCHANGE:
			lval.addr = funcexpr(f, e->base);
			return atomicrmw(f, e->u.builtin.kind, e->type, lval,
			                 funcexpr(f, e->base->next));
		case BUILTINATOMICCOMPAREEXCHANGE:
			return atomiccompareexchange(f, e->base->type->base,
				funcexpr(f, e->base), funcexpr(f, e->base->next),
				funcexpr(f, e->base->next->next));
		case BUILTINATOMICADDASSIGN:
		case BUILTINATOMICSUBASSIGN:
			lval.addr = funcexpr(f, e->base);
			r = funcexpr(f, e->base->next);
			l = atomicrmw(f, e->u.builtin.kind, e->type, lval, r);
			return funcinst(f, e->u.builtin.kind == BUILTINATOMICADDASSIGN ? IADD : ISUB,
			                irtype(e->type).base, l, r);
		case BUILTINVASTART:
			l = funcexpr(f, e->base);
			funcinst(f, IVASTART, 0, l, NULL);
			break;
		case BUILTINVAARG:
			if (e->toeval)
				funcexpr(f, e->toeval);
			/* https://todo.sr.ht/~mcf/cproc/52 */
			if (!(e->type->prop & PROPSCALAR))
				error(&tok.loc, "va_arg with non-scalar type is not yet supported");
			l = funcexpr(f, e->base);
			return funcinst(f, IVAARG, irtype(e->type).base, l, NULL);
		case BUILTINALLOCA:
			l = funcexpr(f, e->base);
			return funcinst(f, IALLOC16, ptrclass(), l, NULL);
		case BUILTINUNREACHABLE:
			funchlt(f);
			return NULL;
		default:
			fatal("internal error: unimplemented builtin");
		}
		return NULL;
	case EXPRTEMP:
		assert(e->u.temp);
		return e->u.temp;
	case EXPRSIZEOF:
		t = e->u.szof.type;
		assert(t->kind == TYPEARRAY);
		calcvla(f, t);
		/* if the sizeof operand has VLA type, we must evaluate it */
		if (e->base)
			funcexpr(f, e->base);
		return t->u.array.size;
	}
	fatal("unimplemented expression %d", e->kind);
	return NULL;
}

static void
zero(struct func *func, struct value *addr, int align, unsigned long long offset,
     unsigned long long end)
{
	static const enum instkind store[] = {
		[1] = ISTOREB,
		[2] = ISTOREH,
		[4] = ISTOREW,
		[8] = ISTOREL,
	};
	static struct value z = {.kind = VALUE_INTCONST};
	struct value *tmp;
	int a = 1;

	while (offset < end) {
		if ((align - (offset & align - 1)) & a) {
			tmp = offset ? funcinst(func, IADD, ptrclass(), addr, mkintconst(offset)) : addr;
			funcinst(func, store[a], 0, &z, tmp);
			offset += a;
		}
		if (a < align)
			a <<= 1;
	}
}

void
funcinit(struct func *func, struct decl *d, struct init *init, bool hasinit)
{
	struct lvalue dst;
	struct value *src, *v;
	unsigned long long offset = 0, max = 0;
	size_t i, w;

	funcalloc(func, d);
	if (!hasinit)
		return;
	for (; init; init = init->next) {
		zero(func, d->value, d->type->align, offset, init->start);
		dst.bits = init->bits;
		if (init->expr->kind == EXPRSTRING) {
			w = init->expr->type->base->size;
			for (i = 0; i < init->expr->u.string.size && i * w < init->end - init->start; ++i) {
				v = mkintconst(init->start + i * w);
				dst.addr = funcinst(func, IADD, ptrclass(), d->value, v);
				switch (w) {
				case 1: v = mkintconst(((unsigned char *)init->expr->u.string.data)[i]); break;
				case 2: v = mkintconst(((uint_least16_t *)init->expr->u.string.data)[i]); break;
				case 4: v = mkintconst(((uint_least32_t *)init->expr->u.string.data)[i]); break;
				}
				funcstore(func, init->expr->type->base, QUALNONE, dst, v);
			}
			offset = init->start + i * w;
		} else {
			if (offset < init->end && (dst.bits.before || dst.bits.after))
				zero(func, d->value, d->type->align, offset, init->end);
			dst.addr = d->value;
			/*
			IR's memopt does not eliminate the store for ptr + 0,
			so only emit the add if the offset is non-zero
			*/
			if (init->start > 0)
				dst.addr = funcinst(func, IADD, ptrclass(), dst.addr, mkintconst(init->start));
			src = funcexpr(func, init->expr);
			funcstore(func, init->expr->type, QUALNONE, dst, src);
			offset = init->end;
		}
		if (max < offset)
			max = offset;
	}
	zero(func, d->value, d->type->align, max, d->type->size);
}
