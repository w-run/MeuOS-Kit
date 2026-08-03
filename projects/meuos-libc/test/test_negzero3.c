#include <stdio.h>
#include <string.h>
#include <stdint.h>

static void
check(const char *fmt, double v, const char *want)
{
	char buf[256];
	snprintf(buf, sizeof buf, fmt, v);
	if (strcmp(buf, want) != 0)
		printf("FAIL: %-10s => [%s], want [%s]\n", fmt, buf, want);
	else
		printf("PASS: %-10s => [%s]\n", fmt, buf);
}

int main(void)
{
	check("%f", -0x0p+0, "-0.000000");
	check("%g", -0x0p+0, "-0");
	return 0;
}