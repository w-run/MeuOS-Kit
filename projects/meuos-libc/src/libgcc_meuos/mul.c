/* mul.c — multiply helpers (libgcc ABI).
 *
 * __muldi3  — signed 64x64 -> 64 (native on x86_64; kept for portability).
 * __multi3  — signed 128x128 -> 128 (the low 128 bits of the product),
 *             done as a 32-bit-limb schoolbook multiply with a single
 *             post-pass carry propagation — needs no __int128 and never
 *             recurses.
 */

#include "libgcc_int.h"

dword
__muldi3(dword a, dword b)
{
	return a * b;
}

static uti_int
umul128(uti_int a, uti_int b)
{
	unsigned a0 = (unsigned)a.lo, a1 = (unsigned)(a.lo >> 32);
	unsigned a2 = (unsigned)a.hi, a3 = (unsigned)(a.hi >> 32);
	unsigned b0 = (unsigned)b.lo, b1 = (unsigned)(b.lo >> 32);
	unsigned b2 = (unsigned)b.hi, b3 = (unsigned)(b.hi >> 32);
	unsigned ia[4] = { a0, a1, a2, a3 };
	unsigned ib[4] = { b0, b1, b2, b3 };
	unsigned long long acc[8];
	unsigned long long carry;
	int i, j, k;

	for (k = 0; k < 8; k++)
		acc[k] = 0;

	/* convolve the 32-bit limbs into 64-bit-wide accumulators.  Each
	 * product a[i]*b[j] contributes (p_hi<<32 | p_lo) at limbs i+j and
	 * i+j+1.  No mid-loop masking: the running sums stay well below 2^64. */
	for (i = 0; i < 4; i++) {
		for (j = 0; j < 4; j++) {
			unsigned long long p = (unsigned long long)ia[i] * ib[j];
			k = i + j;
			acc[k] += (unsigned long long)(unsigned)p;
			acc[k + 1] += (unsigned long long)(unsigned)(p >> 32);
		}
	}

	/* normalise: propagate carries from each limb upward, keeping only the
	 * low 32 bits per limb.  acc[7] upper bits are simply discarded (they
	 * sit at bit 224+, outside the 128-bit result). */
	carry = 0;
	for (k = 0; k < 8; k++) {
		acc[k] += carry;
		carry = acc[k] >> 32;
		if (k < 7)
			acc[k] &= 0xFFFFFFFFu;
	}

	return (uti_int){
		(du_int)acc[0] | ((du_int)acc[1] << 32),
		(du_int)acc[2] | ((du_int)acc[3] << 32)
	};
}

uti_int
__multi3(uti_int a, uti_int b)
{
	return umul128(a, b);
}
