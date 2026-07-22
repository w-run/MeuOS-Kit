/* i386 floating-point arithmetic / conversion regression test.
 *
 * Exercises the code paths that previously crashed the i386 emitter:
 *   - float return + binary ops (the slot-resident float temporary
 *     reaching a %M operand in float_binary: a*b + 1.0);
 *   - float -> long long (fisttpq to a slot destination, %M=);
 *   - float -> unsigned int / unsigned long long (the previously
 *     UNIMPLEMENTED Ostoui/Odtoui ops, now emitted via the
 *     "subtract 2^N, fisttp, add 2^N" x87 correction).
 *
 * Identity helpers defeat constant folding so the runtime emit path is
 * exercised.  Exit 0 on success, nonzero on failure. */

#include <stdio.h>

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static double getd(double x) { return x; }
static float  getf(float x)  { return x; }

/* float return + binary ops (original emit-assert crash path) */
static double fret(double a, double b) { return a * b + 1.0; }

int main(void)
{
	/* binary + return */
	CHECK(fret(getd(3.0), getd(4.0)) == 13.0, "fret 3*4+1");

	/* signed int from float/double */
	CHECK((int)getd(3.9) == 3, "d->int 3.9");
	CHECK((int)getd(-1.9) == -1, "d->int -1.9");
	CHECK((int)getf(2.5f) == 2, "f->int 2.5");

	/* unsigned int from float/double (was: "no match for dtoui") */
	CHECK((unsigned)getd(3.0) == 3u, "d->uint 3");
	CHECK((unsigned)getd(-1.0) == 0xFFFFFFFFu, "d->uint -1");
	CHECK((unsigned)getd(3.0e9) == 3000000000u, "d->uint 3e9");
	CHECK((unsigned)getf(3.0f) == 3u, "f->uint 3");

	/* long long from double (was: %M= assert on fisttpq slot dest) */
	CHECK((long long)getd(2.0) == 2LL, "d->ll 2");
	CHECK((long long)getd(1099511627776.0) == 1099511627776LL, "d->ll 2^40");

	/* unsigned long long from double (was: "no match for dtoui") */
	CHECK((unsigned long long)getd(2.0) == 2ULL, "d->ull 2");
	CHECK((unsigned long long)getd(1099511627776.0) == 1099511627776ULL,
	    "d->ull 2^40");
	CHECK((unsigned long long)getd(-1.0) == 0xFFFFFFFFFFFFFFFFULL, "d->ull -1");
	CHECK((unsigned long long)getf(2.0f) == 2ULL, "f->ull 2");

	if (fails) {
		printf("FAIL: %d float-arith checks failed\n", fails);
		return 1;
	}
	printf("OK: float-arith regression passed\n");
	return 0;
}
