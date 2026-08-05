/* math_extra_gate.c — inverse-trig + memccpy + putenv gate (region 3).
 *
 * Verifies the previously-absent ISO-C math.h inverse-trig family
 * (asin/acos/atan/atan2) against known values, memccpy()'s stop-condition
 * contract, and putenv()'s environment mutation — all of which had no
 * implementation before this region. */
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI  3.141592653589793
#endif

static int fails;

static void
chk(const char *lbl, int cond)
{
	if (!cond) {
		printf("FAIL: %s\n", lbl);
		fails++;
	}
}

static void
chk_approx(const char *lbl, double got, double want, double tol)
{
	double d = got - want;
	if (d < 0) d = -d;
	if (d > tol) {
		printf("FAIL: %s got=%.8g want=%.8g\n", lbl, got, want);
		fails++;
	}
}

int
main(void)
{
	/* atan */
	chk_approx("atan(0)",    atan(0.0),   0.0,       1e-6);
	chk_approx("atan(1)",    atan(1.0),   M_PI/4,    1e-4);
	chk_approx("atan(-1)",   atan(-1.0), -M_PI/4,    1e-4);
	chk_approx("atan(big)",  atan(100.0), 1.5607966, 1e-4);
	chk_approx("atan(0.5)",  atan(0.5),   0.4636476, 1e-4);

	/* atan2 quadrants */
	chk_approx("atan2(1,1)",    atan2(1.0, 1.0),   M_PI/4,    1e-4);
	chk_approx("atan2(1,-1)",   atan2(1.0, -1.0),  3*M_PI/4,  1e-4);
	chk_approx("atan2(-1,-1)",  atan2(-1.0, -1.0), -3*M_PI/4, 1e-4);
	chk_approx("atan2(-1,1)",   atan2(-1.0, 1.0),  -M_PI/4,   1e-4);
	chk_approx("atan2(0,1)",    atan2(0.0, 1.0),   0.0,       1e-4);
	chk_approx("atan2(1,0)",    atan2(1.0, 0.0),   M_PI/2,    1e-4);
	chk_approx("atan2(-1,0)",   atan2(-1.0, 0.0),  -M_PI/2,   1e-4);

	/* asin / acos */
	chk_approx("asin(0)",   asin(0.0),   0.0,      1e-6);
	chk_approx("asin(1)",   asin(1.0),   M_PI/2,   1e-4);
	chk_approx("asin(-1)",  asin(-1.0),  -M_PI/2,  1e-4);
	chk_approx("asin(0.5)", asin(0.5),   0.5235988, 1e-4);
	chk_approx("acos(0)",   acos(0.0),   M_PI/2,   1e-4);
	chk_approx("acos(1)",   acos(1.0),   0.0,      1e-4);
	chk_approx("acos(-1)",  acos(-1.0),  M_PI,     1e-4);
	chk_approx("acos(0.5)", acos(0.5),   1.0471975, 1e-4);

	/* memccpy: stops at (and includes) the sentinel, returns pointer past it */
	{
		const char src[] = "aXb";
		char dst[8] = {0};
		char *p = memccpy(dst, src, 'X', sizeof dst);
		chk("memccpy ptr", p == dst + 2);
		chk("memccpy bytes", memcmp(dst, "aX", 2) == 0);

		/* not found within n -> NULL, n bytes copied */
		memset(dst, 0, sizeof dst);
		const char src2[] = "abcdef";
		p = memccpy(dst, src2, 'Z', sizeof dst);
		chk("memccpy notfound NULL", p == NULL);
		chk("memccpy notfound copied6", memcmp(dst, "abcdef", 6) == 0);
	}

	/* putenv: set NAME=value and observe via getenv */
	{
		char var[] = "MEUOS_TEST_VAR=region3value";
		chk("putenv", putenv(var) == 0);
		const char *v = getenv("MEUOS_TEST_VAR");
		chk("putenv getenv", v && strcmp(v, "region3value") == 0);
	}

	if (fails) {
		printf("%d math_extra FAIL\n", fails);
		return 1;
	}
	printf("PASS math_extra\n");
	return 0;
}
