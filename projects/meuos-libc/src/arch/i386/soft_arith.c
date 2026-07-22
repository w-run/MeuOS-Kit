/* Soft 64-bit arithmetic helpers for i386.
 *
 * The i386 mcc backend supports 64-bit add/sub/neg/copy/load/store but
 * NOT 64-bit mul/div/rem/shift.  These helpers implement the missing
 * operations using only 32-bit arithmetic so that libc functions like
 * printf and strtoull work on i386.
 *
 * 64-bit values are accessed through unsigned* pointer casting to
 * split them into 32-bit halves, avoiding 64-bit shift operations. */

#include <stddef.h>

/* Divide *value by base (32-bit), store quotient back, return remainder. */
unsigned
meuos_u64_divmod(unsigned long long *value, unsigned base)
{
	unsigned *p = (unsigned *)value;
	unsigned hi = p[1];
	unsigned lo = p[0];

	if (hi == 0) {
		unsigned rem = lo % base;
		p[0] = lo / base;
		return rem;
	}
	/* 64/32 unsigned division via shift-subtract. */
	{
		unsigned q_hi = hi / base;
		unsigned rem = hi % base;
		unsigned q_lo = 0;
		unsigned rh = rem;
		unsigned rl = lo;
		int i;
		for (i = 0; i < 32; i++) {
			unsigned carry = rh >> 31;
			rh = (rh << 1) | (rl >> 31);
			rl <<= 1;
			q_lo <<= 1;
			if (rh >= base || carry) {
				rh -= base;
				q_lo |= 1;
			}
		}
		rem = rh;
		p[0] = q_lo;
		p[1] = q_hi;
		return rem;
	}
}

/* Multiply value by mul, return 64-bit result through out_lo/out_hi. */
void
meuos_u64_mul(unsigned value, unsigned mul, unsigned *out_lo, unsigned *out_hi)
{
	unsigned a_lo = value & 0xFFFF;
	unsigned a_hi = value >> 16;
	unsigned b_lo = mul & 0xFFFF;
	unsigned b_hi = mul >> 16;

	unsigned p0 = a_lo * b_lo;
	unsigned p1 = a_lo * b_hi;
	unsigned p2 = a_hi * b_lo;
	unsigned mid = a_hi * b_hi;
	unsigned t;

	/* result = p0 + (p1+p2)<<16 + mid<<32 */
	t = p1 + p2;
	if (t < p1)
		mid += 0x10000;

	/* Add t<<16 to p0 */
	{
		unsigned tshift = t << 16;
		p0 += tshift;
		if (p0 < tshift)
			mid += 1;
	}

	/* Add t>>16 to mid */
	mid += t >> 16;

	*out_lo = p0;
	*out_hi = mid;
}

/* Multiply *value by mul (32-bit) and add addend (32-bit).
 * value = value * mul + addend, using only 32-bit arithmetic. */
void
meuos_u64_mul_add(unsigned long long *value, unsigned mul, unsigned addend)
{
	unsigned *p = (unsigned *)value;
	unsigned lo = p[0], hi = p[1];
	unsigned prod_lo, prod_hi;
	unsigned a_lo = lo & 0xFFFF;
	unsigned a_hi = lo >> 16;
	unsigned b_lo = mul & 0xFFFF;
	unsigned b_hi = mul >> 16;
	unsigned p0 = a_lo * b_lo;
	unsigned p1 = a_lo * b_hi;
	unsigned p2 = a_hi * b_lo;
	unsigned mid = a_hi * b_hi;
	unsigned t;

	t = p1 + p2;
	if (t < p1)
		mid += 0x10000;
	{
		unsigned tshift = t << 16;
		p0 += tshift;
		if (p0 < tshift)
			mid += 1;
	}
	mid += t >> 16;
	prod_lo = p0;
	prod_hi = mid;

	/* hi * mul contributes to upper 32 bits */
	prod_hi += hi * mul;

	/* Add addend */
	prod_lo += addend;
	if (prod_lo < addend)
		prod_hi++;

	p[0] = prod_lo;
	p[1] = prod_hi;
}

/* ---- General 64x64 -> 64 multiply (pure 32-bit) ----
 *
 * Used by the mcc i386 backend for Omul Kl, which the i386 has no
 * native 64-bit multiply for.  Implemented with three 32x32->64
 * partial products (via meuos_u64_mul) summed modulo 2^64.
 *
 *   a = a1*2^32 + a0,  b = b1*2^32 + b0
 *   a*b = a0*b0 + (a0*b1 + a1*b0)<<32  (mod 2^64; a1*b1<<64 dropped)
 */
unsigned long long
meuos_u64_mul64(unsigned long long a, unsigned long long b)
{
	unsigned *pa = (unsigned *)&a, *pb = (unsigned *)&b;
	unsigned a0 = pa[0], a1 = pa[1];
	unsigned b0 = pb[0], b1 = pb[1];
	unsigned p0_lo, p0_hi;          /* a0*b0            */
	unsigned t1_lo, t1_hi;          /* a0*b1 (<<32 term)*/
	unsigned t2_lo, t2_hi;          /* a1*b0 (<<32 term)*/
	unsigned out_lo, out_hi;
	unsigned long long r;

	meuos_u64_mul(a0, b0, &p0_lo, &p0_hi);
	meuos_u64_mul(a0, b1, &t1_lo, &t1_hi);
	meuos_u64_mul(a1, b0, &t2_lo, &t2_hi);

	out_lo = p0_lo;
	/* hi = p0_hi + t1_lo + t2_lo, all occupying bit[32,64); the sum
	 * is truncated to 32 bits (carry into bit[64) is discarded, which
	 * is exactly the 64-bit result truncation). */
	out_hi = p0_hi + t1_lo + t2_lo;

	((unsigned *)&r)[0] = out_lo;
	((unsigned *)&r)[1] = out_hi;
	return r;
}

/* ---- 64/64 unsigned divide + remainder (pure 32-bit) ----
 *
 * Restoring division with a 65-bit running remainder (c:r1:r0);
 * c is the bit above bit 63.  Used by the mcc i386 backend for
 * Odiv/Orem/Oudiv/Ourem Kl.
 */
static void
u64_divrem(const unsigned *n, const unsigned *d, unsigned *q, unsigned *r)
{
	unsigned d0 = d[0], d1 = d[1];
	unsigned q0 = 0, q1 = 0;
	unsigned r0 = 0, r1 = 0;
	int c = 0, i;

	if (d1 == 0 && d0 == 0) {   /* divide by zero: undefined */
		q[0] = q[1] = r[0] = r[1] = 0;
		return;
	}

	for (i = 63; i >= 0; i--) {
		unsigned bit = (i >= 32)
			? ((n[1] >> (i - 32)) & 1u)
			: ((n[0] >> i) & 1u);
		unsigned cl = r0 >> 31;
		r0 = (r0 << 1) | bit;
		unsigned ch = r1 >> 31;
		r1 = (r1 << 1) | cl;
		int nc = (c << 1) | ch;   /* 0..3 */

		if (nc > 0 || r1 > d1 || (r1 == d1 && r0 >= d0)) {
			/* subtract (0,d1,d0) from (c,r1,r0) */
			if (c) {
				unsigned t = r0;
				r0 -= d0;
				if (r0 > t) r1--;
				r1 -= d1;
				c = 0;
			} else {
				unsigned t = r0;
				r0 -= d0;
				if (r0 > t) r1--;
				r1 -= d1;
			}
			if (i >= 32)
				q1 |= (1u << (i - 32));
			else
				q0 |= (1u << i);
		}
	}

	q[0] = q0; q[1] = q1;
	r[0] = r0; r[1] = r1;
}

unsigned long long
meuos_u64_divu(unsigned long long n, unsigned long long d)
{
	unsigned N[2], D[2], Q[2], R[2];
	unsigned long long q;
	N[0] = ((unsigned *)&n)[0]; N[1] = ((unsigned *)&n)[1];
	D[0] = ((unsigned *)&d)[0]; D[1] = ((unsigned *)&d)[1];
	u64_divrem(N, D, Q, R);
	((unsigned *)&q)[0] = Q[0]; ((unsigned *)&q)[1] = Q[1];
	return q;
}

unsigned long long
meuos_u64_remu(unsigned long long n, unsigned long long d)
{
	unsigned N[2], D[2], Q[2], R[2];
	unsigned long long r;
	N[0] = ((unsigned *)&n)[0]; N[1] = ((unsigned *)&n)[1];
	D[0] = ((unsigned *)&d)[0]; D[1] = ((unsigned *)&d)[1];
	u64_divrem(N, D, Q, R);
	((unsigned *)&r)[0] = R[0]; ((unsigned *)&r)[1] = R[1];
	return r;
}

/* ---- Signed 64/64 (pure 32-bit) ----
 *
 * Compute magnitudes via 32-bit halves, divide unsigned, fix sign.
 */
static void
u64_neg(unsigned *x)
{
	x[0] = ~x[0] + 1u;
	x[1] = ~x[1] + (x[0] == 0 ? 1u : 0u);
}

long long
meuos_i64_div(long long a, long long b)
{
	unsigned A[2], B[2], Q[2], R[2];
	int neg = 0;
	long long q;

	A[0] = ((unsigned *)&a)[0]; A[1] = ((unsigned *)&a)[1];
	B[0] = ((unsigned *)&b)[0]; B[1] = ((unsigned *)&b)[1];

	if (A[1] >> 31) { neg ^= 1; u64_neg(A); }
	if (B[1] >> 31) { neg ^= 1; u64_neg(B); }

	u64_divrem(A, B, Q, R);
	((unsigned *)&q)[0] = Q[0]; ((unsigned *)&q)[1] = Q[1];
	return neg ? -q : q;
}

long long
meuos_i64_rem(long long a, long long b)
{
	unsigned A[2], B[2], Q[2], R[2];
	int neg = 0, arem = 0;
	long long r;

	A[0] = ((unsigned *)&a)[0]; A[1] = ((unsigned *)&a)[1];
	B[0] = ((unsigned *)&b)[0]; B[1] = ((unsigned *)&b)[1];

	if (A[1] >> 31) { arem ^= 1; neg ^= 1; u64_neg(A); }
	if (B[1] >> 31) { neg ^= 1; u64_neg(B); }

	u64_divrem(A, B, Q, R);
	((unsigned *)&r)[0] = R[0]; ((unsigned *)&r)[1] = R[1];
	/* C semantics: remainder takes the sign of the dividend (a). */
	return arem ? -r : r;
}

