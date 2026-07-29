/* funcmem.c — Stack allocation, memory load/store, and VLA size calculation.
 *
 * `funcalloc` lowers a local variable declaration to IR's Oalloc4/8/16
 * (stack slot reservation), handling alignment >16 with a manual round-up.
 * `funcstore` / `funcload` deal with scalar stores/loads (with optional
 * bitfield insertion/extraction) and aggregate stores (lowered to a
 * word-by-word copy loop via `funccopy`). `calcvla` walks a variably-
 * modified type chain and emits the IR that computes the runtime byte
 * size of the VLA, storing it on `t->u.array.size` for `funcalloc`. */
#include "irgen.h"

void
calcvla(struct func *f, struct type *t)
{
	struct value *length, *basesize;

	if (!(t->prop & PROPVM))
		return;
	if (t->base)
		calcvla(f, t->base);
	if (t->kind == TYPEFUNC || t->size)
		return;
	assert(t->kind == TYPEARRAY);
	if (!t->u.array.size) {
		assert(t->base->size || t->base->kind == TYPEARRAY);
		assert(t->u.array.length);
		length = convert(f, &typeulong, t->u.array.length->type, funcexpr(f, t->u.array.length));
		basesize = t->base->size ? mkintconst(t->base->size) : t->base->u.array.size;
		t->u.array.size = funcinst(f, IMUL, 'l', length, basesize);
	}
}

void
funcalloc(struct func *f, struct decl *d)
{
	enum instkind op;
	struct block *end;
	struct value *v;
	int align;

	assert(!d->type->incomplete);
	calcvla(f, d->type);
	end = f->end;
	if (d->type->size) {
		f->end = f->start;
		v = mkintconst(d->type->size);
	} else {
		assert(d->type->kind == TYPEARRAY);
		assert(d->type->u.array.size);
		v = d->type->u.array.size;
	}
	align = d->u.obj.align;
	switch (align) {
	case 1:
	case 2:
	case 4:  op = IALLOC4; break;
	case 8:  op = IALLOC8; break;
	default:
		/* Pre-pad so IALLOC16's allocation plus post-alignment
		 * IADD+IAND yields a correctly aligned result.
		 * Both additions are needed: the first ensures a non-zero
		 * allocation from salloc(); the second prevents the IAND
		 * from rounding backward. Wasteful but correct.
		 * TODO: implement alloc32 in IR and use that instead. */
		v = funcinst(f, IADD, ptrclass(), v, mkintconst(align - 16));
		/* fallthrough */
	case 16: op = IALLOC16; break;
	}
	v = funcinst(f, op, ptrclass(), v, NULL);
	if (align > 16) {
		v = funcinst(f, IADD, ptrclass(), v, mkintconst(align - 16));
		v = funcinst(f, IAND, ptrclass(), v, mkintconst(-align));
	}
	d->value = v;
	f->end = end;
}

static void
funccopy(struct func *f, struct value *dst, struct value *src,
         unsigned long long size, int align)
{
	enum instkind load, store;
	int class;
	struct value *tmp, *inc;
	unsigned long long off;

	assert((align & align - 1) == 0);
	class = 'w';
	switch (align) {
	case 1: load = ILOADUB, store = ISTOREB; break;
	case 2: load = ILOADUH, store = ISTOREH; break;
	case 4: load = ILOADW, store = ISTOREW; break;
	default: load = ILOADL, store = ISTOREL, align = 8, class = 'l'; break;
	}
	inc = mkintconst(align);
	off = 0;
	for (;;) {
		tmp = funcinst(f, load, class, src, NULL);
		funcinst(f, store, 0, tmp, dst);
		off += align;
		if (off >= size)
			break;
		src = funcinst(f, IADD, ptrclass(), src, inc);
		dst = funcinst(f, IADD, ptrclass(), dst, inc);
	}
}

struct value *
funcstore(struct func *f, struct type *t, enum typequal tq, struct lvalue lval,
          struct value *v)
{
	struct value *r;
	enum typeprop tp;
	unsigned long long mask;
	struct irtype qt;
	int bits;

	if (tq & QUALVOLATILE)
		error(&tok.loc, "volatile store is not yet supported");
	if (tq & QUALCONST)
		error(&tok.loc, "cannot store to 'const' object");
	tp = t->prop;
	assert(!lval.bits.before && !lval.bits.after || tp & PROPINT);
	r = v;
	switch (t->kind) {
	case TYPESTRUCT:
	case TYPEUNION:
	case TYPEARRAY:
		funccopy(f, lval.addr, v, t->size, t->align);
		break;
	case TYPEPOINTER:
		t = &typeulong;
		/* fallthrough */
	default:
		assert(tp & PROPSCALAR);
		qt = irtype(t);
		bits = lval.bits.before + lval.bits.after;
		if (bits) {
			mask = 0xffffffffffffffffu >> 64 - t->size * 8 + bits << lval.bits.before;
			v = funcinst(f, ISHL, qt.base, v, mkintconst(lval.bits.before));
			r = funcbits(f, t, v, lval.bits);
			v = funcinst(f, IAND, qt.base, v, mkintconst(mask));
			v = funcinst(f, IOR, qt.base, v,
				funcinst(f, IAND, qt.base,
					funcinst(f, qt.load, qt.base, lval.addr, NULL),
					mkintconst(~mask)
				)
			);
		}
		funcinst(f, qt.store, 0, v, lval.addr);
		break;
	}
	return r;
}

struct value *
funcload(struct func *f, struct type *t, struct lvalue lval)
{
	struct value *v;
	struct irtype qt;

	switch (t->kind) {
	case TYPESTRUCT:
	case TYPEUNION:
	case TYPEARRAY:
		return lval.addr;
	}
	qt = irtype(t);
	v = funcinst(f, qt.load, qt.base, lval.addr, NULL);
	return funcbits(f, t, v, lval.bits);
}
