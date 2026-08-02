/* C99 <stdarg.h> variadic overflow paths (§7.15).
 *
 * The SysV x86_64 va_list has two argument sources: the reg_save_area
 * (first 6 GP / 8 FP arguments, spilled by the callee prologue) and the
 * overflow_arg_area (everything beyond that, pushed by the caller).
 * The register path is exercised by varargs.c; this test drives the
 * transition into — and the walk through — the overflow area.
 *
 * Regression guard: the MIR backend advanced overflow_arg_area by the
 * reg_save_area slot width (16 for FP) instead of the SysV stack slot
 * width (8 for every class), so the second overflowed double read from
 * the wrong slot (10 doubles summed to 45 instead of 55).
 */
#include <stdarg.h>

static long
gp_overflow(const char *tag, ...)
{
	va_list ap;
	long total = 0;
	int i;

	/* 8 ints: 6 arrive in registers, 2 overflow onto the stack */
	va_start(ap, tag);
	for (i = 0; i < 8; i++)
		total += va_arg(ap, int);
	va_end(ap);
	return total;
}

static double
fp_overflow(const char *tag, ...)
{
	va_list ap;
	double total = 0;
	int i;

	/* 10 doubles: 8 arrive in XMM registers, 2 overflow onto the stack.
	 * The second overflowed value only reads correctly if the overflow
	 * pointer advanced by 8, not by the 16-byte reg_save_area stride. */
	va_start(ap, tag);
	for (i = 0; i < 10; i++)
		total += va_arg(ap, double);
	va_end(ap);
	return total;
}

static long
mixed_overflow(const char *tag, ...)
{
	va_list ap;
	long isum = 0;
	double fsum = 0;
	int i;

	/* both classes overflow, and the two overflow runs are interleaved
	 * in the same overflow area */
	va_start(ap, tag);
	for (i = 0; i < 8; i++)
		isum += va_arg(ap, int);
	for (i = 0; i < 10; i++)
		fsum += va_arg(ap, double);
	va_end(ap);
	return isum == 36 && fsum == 55.0;
}

static long
named_stack_args(const char *tag, int a, int b, int c, int d, int e, int f,
                 int g, ...)
{
	va_list ap;
	long total;

	/* `g` is the 8th named integer: it exhausts the GP registers and is
	 * itself pushed, so the first vararg starts one slot further up */
	va_start(ap, g);
	total = a + b + c + d + e + f + g;
	total += va_arg(ap, int);
	total += va_arg(ap, int);
	va_end(ap);
	return total;
}

int
main(void)
{
	if (gp_overflow("t", 1, 2, 3, 4, 5, 6, 7, 8) != 36)
		return 1;
	if (fp_overflow("t", 1.0, 2.0, 3.0, 4.0, 5.0,
	                6.0, 7.0, 8.0, 9.0, 10.0) != 55.0)
		return 2;
	if (!mixed_overflow("t", 1, 2, 3, 4, 5, 6, 7, 8,
	                    1.0, 2.0, 3.0, 4.0, 5.0,
	                    6.0, 7.0, 8.0, 9.0, 10.0))
		return 3;
	if (named_stack_args("t", 1, 2, 3, 4, 5, 6, 7, 8, 9) != 45)
		return 4;
	return 0;
}
