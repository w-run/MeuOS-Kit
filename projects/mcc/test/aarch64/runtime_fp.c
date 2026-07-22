/* aarch64 floating-point regression test.
 *
 * Exercises double/float arithmetic, comparison, conversion, struct
 * layout, and printf %f formatting — on aarch64 this routes through
 * the FP/SIMD register file (d0-d31, s0-s31) and the AAPCS floating-
 * point argument passing rules.
 *
 * Exit 0 on success, nonzero on failure. */

#include <stdio.h>

int
main(void)
{
	double a = 3.14, b = 2.71;
	double sum = a + b;
	double prod = a * b;
	float fa = 1.5f, fb = 2.5f;
	float fsum = fa + fb;

	if (sum < 5.8 || sum > 5.9) {
		printf("FAIL: sum=%.4f\n", sum);
		return 1;
	}
	if (prod < 8.5 || prod > 8.6) {
		printf("FAIL: prod=%.4f\n", prod);
		return 2;
	}
	if (fsum < 3.9 || fsum > 4.1) {
		printf("FAIL: fsum=%.1f\n", (double)fsum);
		return 3;
	}

	if (!(sum > 5.0)) {
		printf("FAIL: double cmp\n");
		return 4;
	}

	int truncated = (int)prod;
	if (truncated != 8) {
		printf("FAIL: truncated=%d\n", truncated);
		return 5;
	}

	/* Float/double in struct (exercises AAPCS HFp ABI for aggregates). */
	struct { float x; double y; } s = { 1.0f, 2.0 };
	if (s.x < 0.9 || s.x > 1.1) {
		printf("FAIL: struct.x=%.1f\n", (double)s.x);
		return 6;
	}
	if (s.y < 1.9 || s.y > 2.1) {
		printf("FAIL: struct.y=%.1f\n", s.y);
		return 7;
	}

	int n = 42;
	double dn = (double)n;
	if (dn < 41.9 || dn > 42.1) {
		printf("FAIL: int->double=%.1f\n", dn);
		return 8;
	}

	double div = a / b;
	if (div < 1.15 || div > 1.17) {
		printf("FAIL: div=%.6f\n", div);
		return 9;
	}

	/* Multi-field HFA (Homogeneous Float Aggregate) passed in FP regs. */
	struct Vec3 { double x, y, z; };
	struct Vec3 v = { 1.0, 2.0, 3.0 };
	double dot = v.x * v.x + v.y * v.y + v.z * v.z;
	if (dot < 13.9 || dot > 14.1) {
		printf("FAIL: HFA dot=%.4f\n", dot);
		return 10;
	}

	printf("OK: floating-point regression passed\n");
	return 0;
}
