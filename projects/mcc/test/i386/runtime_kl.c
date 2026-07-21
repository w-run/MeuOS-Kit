/* i386 Kl (64-bit long long) decomposition regression test.
 *
 * Exercises the push/pop EAX/EDX wrapper around all Kl ops to verify
 * that the implicit EAX clobber does not destroy live Kw values that
 * rega allocated to EAX.  The struct-stat pattern (size/mode/atime)
 * is the canonical reproducer: s.mode lives in EAX across a Kl load
 * of s.atime, and must survive the two-movl decomposition.
 *
 * Exit 0 on success, nonzero on failure. */

#include <stdio.h>

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

	printf("OK: Kl decomposition regression passed\n");
	return 0;
}
