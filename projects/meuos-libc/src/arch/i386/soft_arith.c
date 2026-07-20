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
