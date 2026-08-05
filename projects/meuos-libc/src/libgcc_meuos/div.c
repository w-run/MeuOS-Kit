/* div.c — 64/128-bit division and modulo (libgcc ABI).
 *
 * __udivdi3/__divdi3/__umoddi3/__moddi3/__udivmoddi4/__divmoddi4 (64-bit)
 * __udivti3/__divti3/__umodti3/__modti3/__udivmodti4/__divmodti4 (128-bit)
 *
 * Implemented with restore-by-iteration (shift-subtract) over the operand
 * width, so no `/` or `%` on the operand type is ever emitted by a C
 * compiler here (which would recurse into these very helpers).
 */

#include "libgcc_int.h"
#include <stddef.h>

/* ---- unsigned 64/64: q=floor(n/d), r=n-q*d ; d==0 -> q=r=0 (undefined in C) ---- */
static void
udivmod64_inner(du_int n, du_int d, du_int *q, du_int *r)
{
	du_int qq = 0;
	du_int rr = 0;
	int i;

	/*
	 * Skip the high zero-leading bits of the divisor so the running
	 * remainder can't overflow: shift n so that the algorithm only walks
	 * meaningful bits.  For a 64-bit loop this is not strictly needed, but
	 * it keeps the running remainder in range on every iteration.
	 */
	qq = 0;
	rr = 0;
	for (i = 63; i >= 0; i--) {
		int bit = (int)((n >> i) & 1u);
		rr = (rr << 1) | (du_int)bit;
		if (rr >= d) {
			rr -= d;
			qq |= ((du_int)1 << i);
		}
	}
	*q = qq;
	if (r)
		*r = rr;
}

du_int
__udivdi3(du_int n, du_int d)
{
	du_int q, r;
	if (d == 0)
		return 0;
	udivmod64_inner(n, d, &q, &r);
	return q;
}

du_int
__umoddi3(du_int n, du_int d)
{
	du_int q, r;
	if (d == 0)
		return n;
	udivmod64_inner(n, d, &q, &r);
	return r;
}

/* glibc-style combined helper; gcc emits these with -fno-speculative or on
 * some ABIs.  Returns quotient, stores remainder (if non-NULL). */
du_int
__udivmoddi4(du_int n, du_int d, du_int *rem)
{
	du_int q, r;
	if (d == 0) {
		if (rem)
			*rem = n;
		return 0;
	}
	udivmod64_inner(n, d, &q, &r);
	if (rem)
		*rem = r;
	return q;
}

/* ---- signed 64/64: trunc toward zero; remainder takes sign of dividend ---- */
dword
__divdi3(dword a, dword b)
{
	int neg = 0;
	dword q, r;

	if (b == 0)
		return 0;
	if (a < 0) {
		a = __negdi2_op(a);
		neg ^= 1;
	}
	if (b < 0) {
		b = __negdi2_op(b);
		neg ^= 1;
	}
	udivmod64_inner((du_int)a, (du_int)b, (du_int *)&q, (du_int *)&r);
	return neg ? __negdi2_op(q) : q;
}

dword
__moddi3(dword a, dword b)
{
	int neg = 0;
	dword q, r;

	if (b == 0)
		return a;
	if (a < 0) {
		a = __negdi2_op(a);
		neg = 1;
	}
	if (b < 0)
		b = __negdi2_op(b);
	udivmod64_inner((du_int)a, (du_int)b, (du_int *)&q, (du_int *)&r);
	return neg ? __negdi2_op(r) : r;
}

dword
__divmoddi4(dword a, dword b, dword *rem)
{
	int neg = 0;
	dword q, r;

	if (b == 0) {
		if (rem)
			*rem = a;
		return 0;
	}
	if (a < 0) {
		a = __negdi2_op(a);
		neg ^= 1;
	}
	if (b < 0) {
		b = __negdi2_op(b);
		neg ^= 1;
	}
	udivmod64_inner((du_int)a, (du_int)b, (du_int *)&q, (du_int *)&r);
	if (rem)
		*rem = neg ? __negdi2_op(r) : r;
	return neg ? __negdi2_op(q) : q;
}

/* ---- 128/128 unsigned, via 64-bit shift-subtract (restore division) ---- */

static int
uti_ge(uti_int a, uti_int b)
{
	return a.hi > b.hi || (a.hi == b.hi && a.lo >= b.lo);
}

static uti_int
uti_sub(uti_int a, uti_int b)
{
	uti_int r;
	du_int borrow;

	r.lo = a.lo - b.lo;
	borrow = (a.lo < b.lo) ? 1 : 0;
	r.hi = a.hi - b.hi - borrow;
	return r;
}

static void
udivmod128_inner(uti_int n, uti_int d, uti_int *q, uti_int *r)
{
	uti_int qq = {0, 0};
	uti_int rr = {0, 0};
	int i;

	for (i = 127; i >= 0; i--) {
		/* rr = (rr << 1) | bit(n, i) */
		du_int carry = (i >= 64) ? ((n.hi >> (i - 64)) & 1u)
		                         : ((n.lo >> i) & 1u);
		rr = (uti_int){ (rr.lo << 1), (rr.hi << 1) | (rr.lo >> 63) };
		rr.lo |= carry;
		if (uti_ge(rr, d)) {
			rr = uti_sub(rr, d);
			if (i >= 64)
				qq.hi |= ((du_int)1 << (i - 64));
			else
				qq.lo |= ((du_int)1 << i);
		}
	}
	if (q)
		*q = qq;
	if (r)
		*r = rr;
}

uti_int
__udivti3(uti_int n, uti_int d)
{
	uti_int q, r;
	if (d.hi == 0 && d.lo == 0)
		return (uti_int){ 0, 0 };
	udivmod128_inner(n, d, &q, &r);
	return q;
}

uti_int
__umodti3(uti_int n, uti_int d)
{
	uti_int q, r;
	if (d.hi == 0 && d.lo == 0)
		return n;
	udivmod128_inner(n, d, &q, &r);
	return r;
}

uti_int
__udivmodti4(uti_int n, uti_int d, uti_int *rem)
{
	uti_int q, r;
	if (d.hi == 0 && d.lo == 0) {
		if (rem)
			*rem = n;
		return (uti_int){ 0, 0 };
	}
	udivmod128_inner(n, d, &q, &r);
	if (rem)
		*rem = r;
	return q;
}

/* ---- signed 128/128 (trunc toward zero; remainder takes dividend sign) ---- */

static uti_int
uti_neg(uti_int a)
{
	a.lo = ~a.lo;
	a.hi = ~a.hi;
	{
		uti_int one = { 1, 0 };
		uti_int lo;
		lo.lo = a.lo + one.lo;
		lo.hi = a.hi + (a.lo < one.lo ? 1 : 0);
		return lo;
	}
}

static int
uti_neg_p(uti_int a)
{
	return (a.hi >> 63) & 1;
}

uti_int
__divti3(uti_int a, uti_int b)
{
	int neg = 0;
	uti_int q, r;

	if (uti_neg_p(a)) { a = uti_neg(a); neg ^= 1; }
	if (uti_neg_p(b)) { b = uti_neg(b); neg ^= 1; }
	if (b.hi == 0 && b.lo == 0)
		return (uti_int){ 0, 0 };
	udivmod128_inner(a, b, &q, &r);
	return neg ? uti_neg(q) : q;
}

uti_int
__modti3(uti_int a, uti_int b)
{
	int neg = 0;
	uti_int q, r;

	if (uti_neg_p(a)) { a = uti_neg(a); neg = 1; }
	if (uti_neg_p(b)) { b = uti_neg(b); }
	if (b.hi == 0 && b.lo == 0)
		return a;
	udivmod128_inner(a, b, &q, &r);
	return neg ? uti_neg(r) : r;
}

uti_int
__divmodti4(uti_int a, uti_int b, uti_int *rem)
{
	int neg = 0;
	uti_int q, r;

	if (uti_neg_p(a)) { a = uti_neg(a); neg ^= 1; }
	if (uti_neg_p(b)) { b = uti_neg(b); neg ^= 1; }
	if (b.hi == 0 && b.lo == 0) {
		if (rem)
			*rem = a;
		return (uti_int){ 0, 0 };
	}
	udivmod128_inner(a, b, &q, &r);
	if (rem)
		*rem = (neg ? uti_neg(r) : r);
	return neg ? uti_neg(q) : q;
}
