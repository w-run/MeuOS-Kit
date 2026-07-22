/* aarch64 variadic function (va_list) regression test.
 *
 * aarch64 AAPCS va_list is a struct {void *stack; void *gr_top;
 * void *vr_top; int gr_off; int vr_off;} passed by value (or via a
 * stack slot).  This stresses mcc's aarch64 ABI classification (the
 * ishfa Kx guard in aarch64_abi.c) and the cross-function va_list
 * forwarding path that originally tripped sttmps with an empty HFA.
 *
 * Exit 0 on success, nonzero on failure. */

#include <stdio.h>
#include <stdarg.h>

static int
sum_args(int count, ...)
{
	va_list ap;
	va_start(ap, count);
	int sum = 0;
	for (int i = 0; i < count; i++)
		sum += va_arg(ap, int);
	va_end(ap);
	return sum;
}

static void
print_mixed(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	for (const char *p = fmt; *p; p++) {
		if (*p == 'd') printf("%d ", va_arg(ap, int));
		else if (*p == 's') printf("%s ", va_arg(ap, char *));
		else if (*p == 'f') printf("%.1f ", va_arg(ap, double));
		else if (*p == 'l') printf("%lld ", va_arg(ap, long long));
	}
	va_end(ap);
	printf("\n");
}

/* Cross-function va_list forwarding (aarch64 va_list is a 32-byte
 * struct passed by value, so forwarding must copy all 5 fields). */
static int
vsum_helper(va_list ap, int count)
{
	int sum = 0;
	for (int i = 0; i < count; i++)
		sum += va_arg(ap, int);
	return sum;
}

static int
vsum(int count, ...)
{
	va_list ap;
	va_start(ap, count);
	int result = vsum_helper(ap, count);
	va_end(ap);
	return result;
}

int
main(void)
{
	if (sum_args(5, 10, 20, 30, 40, 50) != 150) {
		printf("FAIL: sum_args\n");
		return 1;
	}
	if (vsum(3, 100, 200, 300) != 600) {
		printf("FAIL: vsum cross-function\n");
		return 2;
	}

	long long big = 9999999999LL;
	if (big != 9999999999LL) {
		printf("FAIL: long long=%lld\n", big);
		return 3;
	}

	char buf[64];
	int n = snprintf(buf, sizeof buf, "%d %s %f %lld %x %c",
		1, "two", 3.0, 4LL, 0xff, 'A');
	if (n < 0) {
		printf("FAIL: snprintf\n");
		return 4;
	}

	/* Empty va_list forwarding (the aarch64 ishfa Kx guard reproducer). */
	if (vsum(0) != 0) {
		printf("FAIL: vsum empty\n");
		return 5;
	}

	printf("OK: va_list regression passed\n");
	return 0;
}
