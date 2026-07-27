/* aeabi.c — ARM EABI runtime helpers (division, memory ops).
 *
 * These are REQUIRED on ARM 32-bit: the compiler emits __aeabi_* calls for
 * integer division, struct copies, etc.  The 64-bit-div/mod helpers delegate
 * to pure-32-bit C algorithms (udivmod64/divmod64) so they do NOT emit
 * recursive __aeabi calls when compiled by mcc (or by cross-gcc picking up
 * libgcc), making them self-contained for a zero-GNU toolchain.
 *
 * The 64-bit division wrappers that return quotient AND remainder via
 * registers (r0/r1/r2/r3) live in aeabi_wrap.S; this file provides the
 * actual algorithm plus the 32-bit wrappers whose return layout matches
 * the EABI directly (r0/r1 for a 2-uint32 struct). */

#include <stdint.h>

/* ---- internal unsigned 64-bit divmod (pure 32-bit ops, no __aeabi emission) ---- */

/* Return true if the unsigned 128-bit value (a0,a1) >= (b0,b1) (both 64-bit). */
static int
ge64(uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1)
{
	return a1 > b1 || (a1 == b1 && a0 >= b0);
}

/* Subtract d from *a (both 64-bit), assuming *a >= d. */
static void
sub64(uint32_t *a0, uint32_t *a1, uint32_t d0, uint32_t d1)
{
	uint32_t borrow;
	if (*a0 < d0)
		borrow = 1, *a0 = *a0 - d0;
	else
		borrow = 0, *a0 = *a0 - d0;
	*a1 = *a1 - borrow - d1;
}

uint64_t
udivmod64(uint64_t n, uint64_t d, uint64_t *rem)
{
	uint32_t nl = (uint32_t)n, nh = (uint32_t)(n >> 32);
	uint32_t dl = (uint32_t)d, dh = (uint32_t)(d >> 32);
	uint32_t ql = 0, qh = 0;   /* quotient */
	uint32_t rl = 0, rh = 0;   /* remainder */
	int i;

	if (d == 0) {
		*rem = n;        /* div-by-zero: return n as remainder */
		return 0;        /* q = 0 */
	}

	for (i = 63; i >= 0; i--) {
		uint32_t carry = (nh >> 31) & 1;
		/* Shift R left 1, bring in top bit of N */
		rh = (rh << 1) | (nl >> 31);
		rl = (rl << 1) | carry;
		/* Shift N left 1 */
		nh = (nh << 1) | (nl >> 31);
		nl = (nl << 1) & 0x7fffffff;  /* discard shifted-out bit (it went to carry and R) */
		if (ge64(rl, rh, dl, dh)) {
			sub64(&rl, &rh, dl, dh);
			if (i >= 32)
				qh |= (uint32_t)(1u << (i - 32));
			else
				ql |= (uint32_t)(1u << i);
		}
	}
	if (rem)
		*rem = ((uint64_t)rh << 32) | rl;
	return ((uint64_t)qh << 32) | ql;
}

/* ---- internal signed 64-bit divmod ---- */
int64_t
divmod64(int64_t n, int64_t d, int64_t *rem)
{
	int neg = 0;
	int64_t q;

	if (n < 0) { n = -n; neg = 1; }
	if (d < 0) { d = -d; neg = !neg; }
	q = (int64_t)udivmod64((uint64_t)n, (uint64_t)d, (uint64_t *)rem);
	if (neg) {
		q = -q;
		if (rem) *rem = -(int64_t)*rem;
	} else {
		if (rem && n < 0) *rem = -(int64_t)*rem;
	}
	return q;
}

/* ---- 32-bit division helpers (EABI return layout) ---- */

uint32_t
__aeabi_uidiv(uint32_t n, uint32_t d)
{
	uint32_t i, q = 0;
	uint64_t r = 0;  /* use 64-bit for simplicity; compiled with C's | / << / & which are 32-bit */

	if (d == 0) return 0;
	/* simple shift-subtract using 64-bit r */
	for (i = 32; i > 0; i--) {
		r <<= 1;
		r |= (n >> 31) & 1;
		n <<= 1;
		if (r >= d) {
			r -= d;
			q |= (1u << (i - 1));
		}
	}
	return q;
}
int32_t
__aeabi_idiv(int32_t n, int32_t d)
{
	int neg = 0;
	if (n < 0) { n = -n; neg = 1; }
	if (d < 0) { d = -d; neg = !neg; }
	return neg ? -(int)__aeabi_uidiv((uint32_t)n, (uint32_t)d)
	           : (int)__aeabi_uidiv((uint32_t)n, (uint32_t)d);
}

struct u32pair { uint32_t q; uint32_t r; };
struct s32pair { int32_t q; int32_t r; };

struct u32pair
__aeabi_uidivmod(uint32_t n, uint32_t d)
{
	uint32_t q;
	uint64_t r = 0;
	uint32_t i;

	if (d == 0) return (struct u32pair){0, n};
	for (i = 32; i > 0; i--) {
		r <<= 1;
		r |= (n >> 31) & 1;
		n <<= 1;
		if (r >= d) {
			r -= d;
			q |= (1u << (i - 1));
		}
	}
	return (struct u32pair){q, (uint32_t)r};
}
struct s32pair
__aeabi_idivmod(int32_t n, int32_t d)
{
	int neg = 0;
	uint32_t q, r;
	if (n < 0) { n = -n; neg = 1; }
	if (d < 0) { d = -d; neg = !neg; }
	/* note: we're using uint32_t __aeabi_uidivmod — but to avoid double call, inline */
	r = 0; q = 0;
	uint32_t un = (uint32_t)n, ud = (uint32_t)d;
	uint32_t i;
	if (ud == 0) { r = un; q = 0; }
	else for (i = 32; i > 0; i--) {
		uint64_t tr = r;
		tr <<= 1; tr |= (un >> 31) & 1; un <<= 1;
		r = (uint32_t)tr;
		if (r >= ud) { r -= ud; q |= (1u << (i - 1)); }
	}
	if (neg) { q = -q; r = -r; }
	return (struct s32pair){(int32_t)q, (int32_t)r};
}
/* ---- memory helpers (compiler may emit __aeabi_memcpy etc. for struct copys) ---- */

void
__aeabi_memcpy(void *dest, const void *src, size_t n)
{
	unsigned char *d = dest;
	const unsigned char *s = src;
	while (n--) *d++ = *s++;
}

void
__aeabi_memmove(void *dest, const void *src, size_t n)
{
	/* memmove handles overlap correctly; use byte-by-byte for simplicity */
	unsigned char *d = dest;
	const unsigned char *s = src;
	if (d < s)
		while (n--) *d++ = *s++;
	else {
		d += n; s += n;
		while (n--) *--d = *--s;
	}
}

void
__aeabi_memset(void *dest, int c, size_t n)
{
	unsigned char *d = dest;
	while (n--) *d++ = (unsigned char)c;
}

void
__aeabi_memclr(void *dest, size_t n)
{
	__aeabi_memset(dest, 0, n);
}
