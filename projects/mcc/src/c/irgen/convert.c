/* convert.c — Bitfield extraction and arithmetic type conversion.
 *
 * `funcbits` shifts/masks a value to extract or insert a bitfield whose
 * storage unit is wider than the declared type (C11 6.7.2.1).
 * `convert` emits the IR op sequence for C's "usual arithmetic
 * conversions" (C11 6.3.1.8) — int promotions, signed/zero extensions,
 * float↔int casts, single↔double narrowing. The op vocabulary here is
 * shared with the C frontend's `struct inst` (enum instkind, IXXX) and
 * is mapped to IR ops (OXXX) at emit time by emit.c's fe_to_ir_op(). */
#include "irgen.h"

struct value *
funcbits(struct func *f, struct type *t, struct value *v, struct bitfield b)
{
	int class, bits;

	class = t->size <= 4 ? 'w' : 'l';
	bits = b.after;
	if (!bits && t->kind == TYPEBITINT)
		bits = t->size * 8 - t->u.arith.width;
	if (bits) {
		bits += (t->size + 3 & -4) - t->size << 3;
		v = funcinst(f, ISHL, class, v, mkintconst(bits));
	}
	bits += b.before;
	if (bits)
		v = funcinst(f, t->u.arith.issigned ? ISAR : ISHR, class, v, mkintconst(bits));
	return v;
}

struct value *
convert(struct func *f, struct type *dst, struct type *src, struct value *l)
{
	enum instkind op;
	struct value *r = NULL;
	int class;

	if (src == dst)
		return l;
	if (src->kind == TYPEATOMIC)
		src = src->base;
	if (dst->kind == TYPEATOMIC)
		dst = dst->base;
	if (src == dst)
		return l;
	if (src->kind == TYPENULLPTR && (dst->kind == TYPEBOOL ||
	    dst->kind == TYPEPOINTER || dst->kind == TYPENULLPTR))
		/* nullptr_t holds only the null pointer constant: converting it
		 * to bool (false), to a pointer (null) or to nullptr_t yields 0. */
		return mkintconst(0);
	if (src->kind == TYPEPOINTER)
		src = &typeulong;
	if (dst->kind == TYPEPOINTER)
		dst = &typeulong;
	if (dst->kind == TYPEVOID)
		return NULL;
	if (!(src->prop & PROPREAL) || !(dst->prop & PROPREAL))
		fatal("internal error; unsupported conversion");
	if (dst->kind == TYPEBOOL) {
		class = 'w';
		if (src->prop & PROPINT) {
			r = mkintconst(0);
			switch (src->size) {
			case 1: op = ICNEW; if (src->kind != TYPEBITINT) l = funcinst(f, IEXTUB, 'w', l, NULL); break;
			case 2: op = ICNEW; if (src->kind != TYPEBITINT) l = funcinst(f, IEXTUH, 'w', l, NULL); break;
			case 4: op = ICNEW; break;
			case 8: op = ICNEL; break;
			default: fatal("internal error; unknown integer conversion");
			}
		} else {
			assert(src->prop & PROPFLOAT);
			switch (src->size) {
			case 4: op = ICNES, r = mkfltconst(VALUE_FLTCONST, 0); break;
			case 8: op = ICNED, r = mkfltconst(VALUE_DBLCONST, 0); break;
			default: fatal("internal error; unknown floating point conversion");
			}
		}
	} else if (dst->prop & PROPINT) {
		class = dst->size == 8 ? 'l' : 'w';
		if (src->prop & PROPINT) {
			if (dst->u.arith.width <= src->u.arith.width) {
				if (dst->kind == TYPEBITINT)
					return funcbits(f, dst, l, (struct bitfield){0});
				if (dst->u.arith.width == src->u.arith.width)
					return l;
				/* Narrowing conversion (int -> char/short, long long ->
				 * short, ...): mask the source to the destination width
				 * and, for a signed destination, sign-extend the
				 * truncated result.  Without this the wide value is
				 * carried through untouched and the high bits leak into
				 * the result (chibicc B-class bug: constant folding
				 * ignored the narrowing cast). */
				{
					unsigned width = dst->u.arith.width;
					int c = src->size == 8 ? 'l' : 'w';
					struct value *mask =
					    mkintconst(((uint64_t)1 << width) - 1);
					l = funcinst(f, IAND, c, l, mask);
					if (dst->u.arith.issigned) {
						unsigned shift = src->size * 8 - width;
						l = funcinst(f, ISHL, c, l,
						    mkintconst(shift));
						l = funcinst(f, ISAR, c, l,
						    mkintconst(shift));
					}
					return l;
				}
			}
			if (src->kind == TYPEBITINT && (src->size == 8 || dst->size == 4))
				return l;
			switch (src->size) {
			case 4: op = src->u.arith.issigned ? IEXTSW : IEXTUW; break;
			case 2: op = src->u.arith.issigned ? IEXTSH : IEXTUH; break;
			case 1: op = src->u.arith.issigned ? IEXTSB : IEXTUB; break;
			default: fatal("internal error; unknown integer conversion");
			}
		} else {
			if (dst->u.arith.issigned)
				op = src->size == 8 ? IDTOSI : ISTOSI;
			else
				op = src->size == 8 ? IDTOUI : ISTOUI;
		}
	} else {
		class = dst->size == 8 ? 'd' : 's';
		if (src->prop & PROPINT) {
			if (src->size < 4)
				l = convert(f, src->u.arith.issigned ? &typeint : &typeuint, src, l);
			if (src->u.arith.issigned)
				op = src->size == 8 ? ISLTOF : ISWTOF;
			else
				op = src->size == 8 ? IULTOF : IUWTOF;
		} else {
			assert(src->prop & PROPFLOAT);
			if (src->size == dst->size)
				return l;
			op = src->size < dst->size ? IEXTS : ITRUNCD;
		}
	}

	return funcinst(f, op, class, l, r);
}
