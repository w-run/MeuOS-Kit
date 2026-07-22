/* i386 unsigned-integer → floating-point (x87) regression test.
 *
 * Exercises Ouwtof (unsigned 32-bit → float/double) and Oultof
 * (unsigned 64-bit → float/double), including the >= 2^31 and
 * >= 2^63 edges where the signed fildq path must be corrected.
 *
 * Values are obtained through identity helper functions so the
 * conversions are NOT constant-folded and the runtime emit path
 * (fildq / branch) is actually exercised.
 *
 * Exit 0 on success, nonzero on failure. */

#include <stdio.h>

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static unsigned getu(unsigned x) { return x; }
static unsigned long long getull(unsigned long long x) { return x; }

int main(void)
{
	unsigned u = getu(0xFFFFFFFFu);
	double du = (double)u;
	CHECK(du > 4294967295.0*0.9999999 && du < 4294967295.0*1.0000001, "uwtof 0xFFFFFFFF");

	u = getu(0x80000000u);
	double du2 = (double)u;
	CHECK(du2 > 2147483648.0*0.9999999 && du2 < 2147483648.0*1.0000001, "uwtof 2^31");

	float fu = (float)u;
	CHECK(fu > 2147483648.0f*0.99999f && fu < 2147483648.0f*1.00001f, "uwtof float 2^31");

	u = getu(0u);
	double du0 = (double)u;
	CHECK(du0 == 0.0, "uwtof 0");

	unsigned long long big = getull(0xFFFFFFFFFFFFFFFFull);
	double db = (double)big;
	CHECK(db > 1.8446744e19 && db < 1.8446745e19, "ultof 0xFFFFFFFFFFFFFFFF");

	unsigned long long h = getull(0x8000000000000000ull);
	double dh = (double)h;
	CHECK(dh > 9.22337e18 && dh < 9.22338e18, "ultof 2^63");

	unsigned long long mid = getull(123456789012345ull);
	double dmid = (double)mid;
	CHECK(dmid > 1.2345678e14 && dmid < 1.2345680e14, "ultof mid");

	float fh = (float)h;
	CHECK(fh > 9.22337e18f && fh < 9.22338e18f, "ultof float 2^63");

	unsigned long long z = getull(0ull);
	double dz = (double)z;
	CHECK(dz == 0.0, "ultof 0");

	if (fails) {
		printf("FAIL: %d unsigned-to-float checks failed\n", fails);
		return 1;
	}
	printf("OK: unsigned-to-float regression passed\n");
	return 0;
}
