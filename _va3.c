#include <stdio.h>
#include <stdarg.h>

static void print_mixed(const char *fmt, ...)
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

int main(void)
{
	print_mixed("dsfl", 1, "two", 3.0, 4LL);
	printf("OK\n");
	return 0;
}