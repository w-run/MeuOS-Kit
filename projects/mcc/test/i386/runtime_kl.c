/* i386 Kl (64-bit long long) decomposition regression test.
 *
 * Exercises the push/pop EAX/EDX wrapper around all Kl ops to verify
 * that the implicit EAX clobber does not destroy live Kw values that
 * rega allocated to EAX.  The struct-stat pattern (size/mode/atime)
 * is the canonical reproducer: s.mode lives in EAX across a Kl load
 * of s.atime, and must survive the two-movl decomposition.
 *
 * Also exercises 64-bit multiply, divide, and remainder via libc
 * soft-arithmetic calls (meuos_u64_mul64, meuos_u64_divu, etc.)
 * which the i386 backend lowers to when KBASE(Kl) cannot be
 * done natively.
 *
 * Exit 0 on success, nonzero on failure. */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

struct S {
	int size;
	int mode;
	long long atime;
};

__attribute__((noinline))
static int fill(struct S *s)
{
	s->size  = 2387;
	s->mode  = 33188;	/* 0100644 octal */
	s->atime = 1784629447LL;
	return 0;
}

static void check(int cond, int code, const char *msg, long long val, long long exp)
{
	if (!cond) {
		printf("FAIL: %s got %lld expected %lld (code %d)\n",
			msg, val, exp, code);
		exit(code);
	}
}

/* noinline: keep the union store/load pair from being folded away so
 * the Kl <-> double bitcast (Ocast) is actually exercised. */
__attribute__((noinline))
static unsigned long long dbl_to_bits(double d)
{
	union { double d; unsigned long long u; } u;
	u.d = d;
	return u.u;
}

__attribute__((noinline))
static double bits_to_dbl(unsigned long long u)
{
	union { double d; unsigned long long u; } uu;
	uu.u = u;
	return uu.d;
}

int main(void)
{
	struct S s;
	fill(&s);

	/* Kl load of s.atime must not clobber Kw s.mode in EAX. */
	if (s.mode != 33188) {
		printf("FAIL: mode=%d expected 33188\n", s.mode);
		return 1;
	}
	if (s.atime != 1784629447LL) {
		printf("FAIL: atime=%lld expected 1784629447\n", s.atime);
		return 1;
	}
	if (s.size != 2387) {
		printf("FAIL: size=%d expected 2387\n", s.size);
		return 1;
	}

	/* Kl arithmetic: add, sub, neg, and, or, xor, shifts. */
	long long a = 0x100000000LL;	/* requires 64 bits */
	long long b = a + 1;
	if (b != 0x100000001LL) {
		printf("FAIL: Kl add %lld\n", b);
		return 2;
	}
	long long c = b - a;
	if (c != 1) {
		printf("FAIL: Kl sub %lld\n", c);
		return 3;
	}
	long long d = -a;
	if (d != -0x100000000LL) {
		printf("FAIL: Kl neg %lld\n", d);
		return 4;
	}
	long long e = a << 1;
	if (e != 0x200000000LL) {
		printf("FAIL: Kl shl %lld\n", e);
		return 5;
	}
	long long f = e >> 1;
	if (f != a) {
		printf("FAIL: Kl shr %lld\n", f);
		return 6;
	}
	long long g = a | 0xFF;
	if (g != 0x1000000FFLL) {
		printf("FAIL: Kl or %lld\n", g);
		return 7;
	}
	long long h = g & 0xFF;
	if (h != 0xFF) {
		printf("FAIL: Kl and %lld\n", h);
		return 8;
	}
	long long i = g ^ h;
	if (i != 0x100000000LL) {
		printf("FAIL: Kl xor %lld\n", i);
		return 9;
	}

	/* Kl comparison. */
	if (!(a < b)) {
		printf("FAIL: Kl cmp lt\n");
		return 10;
	}
	if (!(a != c)) {
		printf("FAIL: Kl cmp ne\n");
		return 11;
	}

	/* ---- Kl mul (64-bit multiply) ---- */

	/* Simple 32*32 fits in 64 */
	long long m1 = 100000LL * 200000LL;
	check(m1 == 20000000000LL, 20, "Kl mul simple", m1, 20000000000LL);

	/* 64-bit multiply with overflow (0x100000001 * 2 = 0x200000002) */
	long long m2 = 0x100000001LL * 2LL;
	check(m2 == 0x200000002LL, 21, "Kl mul 64x2", m2, 0x200000002LL);

	/* Negative * positive */
	long long m3 = -5LL * 7LL;
	check(m3 == -35LL, 22, "Kl mul neg", m3, -35LL);

	/* Negative * negative */
	long long m4 = -0x100000001LL * -1LL;
	check(m4 == 0x100000001LL, 23, "Kl mul neg*neg", m4, 0x100000001LL);

	/* 64-bit overflow: 0xFFFFFFFF * 0x100000001 = 0xFFFFFFFF00000001 mod 2^64 */
	/* 0xFFFFFFFF * 0x100000001 = (2^32-1)*(2^32+1) = 2^64-1 = 0xFFFFFFFFFFFFFFFF */
	long long m5 = 0xFFFFFFFFLL * 0x100000001LL;
	check(m5 == 0xFFFFFFFFFFFFFFFFLL, 24, "Kl mul overflow", m5, 0xFFFFFFFFFFFFFFFFLL);

	/* Multiply by zero */
	long long m6 = a * 0LL;
	check(m6 == 0LL, 25, "Kl mul zero", m6, 0LL);

	/* ---- Kl div (signed 64-bit) ---- */

	/* Simple 64/32 */
	long long d1 = 0x100000000LL / 2LL;
	check(d1 == 0x80000000LL, 30, "Kl div simple", d1, 0x80000000LL);

	/* Negative / positive */
	long long d2 = -100LL / 3LL;
	check(d2 == -33LL, 31, "Kl div neg/pos", d2, -33LL);

	/* Positive / negative */
	long long d3 = 100LL / -3LL;
	check(d3 == -33LL, 32, "Kl div pos/neg", d3, -33LL);

	/* Negative / negative */
	long long d4 = -100LL / -3LL;
	check(d4 == 33LL, 33, "Kl div neg/neg", d4, 33LL);

	/* Large 64-bit value */
	long long d5 = 0x7FFFFFFFFFFFFFFFLL / 0x100000001LL;
	check(d5 == 0x7FFFFFFFLL, 34, "Kl div large", d5, 0x7FFFFFFFLL);

	/* ---- Kl rem (signed 64-bit) ---- */

	long long r1 = 100LL % 3LL;
	check(r1 == 1LL, 40, "Kl rem simple", r1, 1LL);

	long long r2 = 100LL % -3LL;
	check(r2 == 1LL, 41, "Kl rem pos/neg", r2, 1LL);

	long long r3 = -100LL % 3LL;
	check(r3 == -1LL, 42, "Kl rem neg/pos", r3, -1LL);

	long long r4 = -100LL % -3LL;
	check(r4 == -1LL, 43, "Kl rem neg/neg", r4, -1LL);

	/* ---- Kl udiv (unsigned 64-bit) ---- */

	unsigned long long u1 = 0x100000001ULL / 2ULL;
	check((long long)u1 == 0x80000000LL, 50, "Kl udiv simple", (long long)u1, 0x80000000LL);

	/* MAX_U64 / 2 */
	unsigned long long u2 = 0xFFFFFFFFFFFFFFFFULL / 2ULL;
	check((long long)u2 == 0x7FFFFFFFFFFFFFFFLL, 51,
		"Kl udiv max/2", (long long)u2, 0x7FFFFFFFFFFFFFFFLL);

	/* ---- Kl urem (unsigned 64-bit) ---- */

	unsigned long long u3 = 0xFFFFFFFFFFFFFFFFULL % 10ULL;
	check((long long)u3 == 5LL, 60, "Kl urem max", (long long)u3, 5LL);

	unsigned long long u4 = 0x100000001ULL % 0x100000000ULL;
	check((long long)u4 == 1LL, 61, "Kl urem 64", (long long)u4, 1LL);

	/* ---- Kl bitcast (Ocast): double <-> int64 via union ---- */
	if (dbl_to_bits(-3.5) != 0xC00C000000000000ULL) {
		printf("FAIL: Kl d2u bitcast\n");
		return 70;
	}
	if (dbl_to_bits(bits_to_dbl(0xC00C000000000000ULL)) != 0xC00C000000000000ULL) {
		printf("FAIL: Kl u2d bitcast\n");
		return 71;
	}
	if (dbl_to_bits(1.0) != 0x3FF0000000000000ULL) {
		printf("FAIL: Kl d2u 1.0 bitcast\n");
		return 72;
	}

	printf("OK: Kl decomposition regression passed\n");
	return 0;
}
