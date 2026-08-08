/* va_test.c — cross-arch varargs/va_list matrix.
 *
 * Exercises, on every mcc backend (x86_64/aarch64/riscv64/i386/
 * loongarch64/arm):
 *   - int varargs in the register window and spilling to the stack
 *   - long long varargs (8-byte alignment)
 *   - double varargs in the FP register window and spilling to the stack
 *   - mixed int/double interleaving (register-file split)
 *   - char-star / pointer varargs
 *   - float arguments (promoted to double per C default promotions)
 *   - cross-function va_list forwarding
 *   - nested varargs function calls (callee itself is variadic)
 *
 * The test is scalar-only: mcc rejects non-scalar va_arg (expr.c).
 * Results are reported through integer return codes so the exit status
 * alone is sufficient; every case also prints its name for inspection.
 *
 * Exit 0 on success, nonzero on failure.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static int failures = 0;

static void
check(int cond, const char *name)
{
	if (!cond) {
		printf("FAIL: %s\n", name);
		failures++;
	} else {
		printf("ok:   %s\n", name);
	}
}

/* 1. ints in the register window */
static int
sum_int(int n, ...)
{
	va_list ap;
	va_start(ap, n);
	int s = 0;
	for (int i = 0; i < n; i++)
		s += va_arg(ap, int);
	va_end(ap);
	return s;
}

/* 2. many ints: spills past the register window onto the stack */
static long
sum_int_many(int n, ...)
{
	va_list ap;
	va_start(ap, n);
	long s = 0;
	for (int i = 0; i < n; i++)
		s += va_arg(ap, int);
	va_end(ap);
	return s;
}

/* 3. long long varargs */
static long long
sum_ll(int n, ...)
{
	va_list ap;
	va_start(ap, n);
	long long s = 0;
	for (int i = 0; i < n; i++)
		s += va_arg(ap, long long);
	va_end(ap);
	return s;
}

/* 4. doubles in the FP register window */
static int
sum_double(int n, ...)
{
	va_list ap;
	va_start(ap, n);
	double s = 0;
	for (int i = 0; i < n; i++)
		s += va_arg(ap, double);
	va_end(ap);
	return (int)s;
}

/* 5. many doubles: spills past the FP register window */
static int
sum_double_many(int n, ...)
{
	va_list ap;
	va_start(ap, n);
	double s = 0;
	for (int i = 0; i < n; i++)
		s += va_arg(ap, double);
	va_end(ap);
	return (int)s;
}

/* 6. mixed int/double interleaving — register-file split stress */
static int
mix4(int a, ...)
{
	va_list ap;
	va_start(ap, a);
	int i1 = va_arg(ap, int);
	double d1 = va_arg(ap, double);
	int i2 = va_arg(ap, int);
	double d2 = va_arg(ap, double);
	int i3 = va_arg(ap, int);
	va_end(ap);
	return i1 + (int)d1 + i2 + (int)d2 + i3;
}

/* 7. pointer + int + double + pointer */
static int
strs(const char *tag, ...)
{
	va_list ap;
	va_start(ap, tag);
	const char *a = va_arg(ap, const char *);
	int b = va_arg(ap, int);
	double c = va_arg(ap, double);
	const char *d = va_arg(ap, const char *);
	va_end(ap);
	return (int)strlen(a) + (int)strlen(d) + b + (int)c;
}

/* 8. cross-function va_list forwarding */
static int
vsum_helper(va_list ap, int count)
{
	int s = 0;
	for (int i = 0; i < count; i++)
		s += va_arg(ap, int);
	return s;
}

static int
vsum(int count, ...)
{
	va_list ap;
	va_start(ap, count);
	int r = vsum_helper(ap, count);
	va_end(ap);
	return r;
}

/* 9. float arguments: promoted to double by default promotions */
static int
fsum(int n, ...)
{
	va_list ap;
	va_start(ap, n);
	double s = 0;
	for (int i = 0; i < n; i++)
		s += va_arg(ap, double);   /* float args arrive as double */
	va_end(ap);
	return (int)s;
}

/* 10. nested variadic: a variadic callee called from a variadic body */
static int
nest(int n, ...)
{
	va_list ap;
	va_start(ap, n);
	/* forward the first two ints into another variadic function */
	int r = sum_int(n, va_arg(ap, int), va_arg(ap, int));
	va_end(ap);
	return r;
}

int
main(void)
{
	/* register window (≤ 6 on x86_64 / 8 elsewhere) */
	check(sum_int(5, 10, 20, 30, 40, 50) == 150, "sum_int reg");
	/* register window + stack spill */
	check(sum_int_many(12, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12) == 78,
	      "sum_int_many spill");

	check(sum_ll(4, 1LL, 10000000000LL, 2LL, -30000000000LL) ==
	      -19999999997LL, "sum_ll reg");
	check(sum_ll(10, 1LL, 2LL, 3LL, 4LL, 5LL, 6LL, 7LL, 8LL, 9LL, 10LL) ==
	      55LL, "sum_ll spill");

	check(sum_double(4, 1.0, 2.0, 3.0, 4.0) == 10, "sum_double reg");
	check(sum_double_many(12, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0,
	                      7.0, 8.0, 9.0, 10.0, 11.0, 12.0) == 78,
	      "sum_double spill");

	/* i1=1, d1=2.0, i2=3, d2=4.0, i3=5 -> 1+2+3+4+5 */
	check(mix4(99, 1, 2.0, 3, 4.0, 5) == 15, "mix4 int/double");

	check(strs("hdr", "abcd", 100, 2.0, "xy") == 4 + 2 + 100 + 2,
	      "strs ptr/int/double");

	check(vsum(5, 1, 2, 3, 4, 5) == 15, "vsum cross-func");
	check(vsum(0) == 0, "vsum empty");

	check(fsum(3, 1.5f, 2.5f, 3.0f) == 7, "fsum float promote");
	check(fsum(2, 0.5f, 0.5f) == 1, "fsum float halves");

	check(nest(2, 1, 2) == 3, "nest variadic callee");

	if (failures == 0)
		printf("ALL PASS\n");
	return failures;
}
