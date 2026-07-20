#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int
compare_int(const void *left, const void *right)
{
	int a = *(const int *)left;
	int b = *(const int *)right;
	return (a > b) - (a < b);
}

int
main(void)
{
	char *end;
	int values[4] = { 4, 1, 3, 2 };
	int key = 3;
	char *copy;
	if (strtol(" -42z", &end, 10) != -42 || *end != 'z')
		return 1;
	if (strtoll("0x7fffffff", &end, 0) != 2147483647LL || *end)
		return 1;
	if (strtoull("177", &end, 8) != 127ULL || *end)
		return 1;
	if (abs(-7) != 7 || labs(-9L) != 9L || llabs(-11LL) != 11LL)
		return 1;
	if (strtod("-12.5e1x", &end) != -125.0 || *end != 'x')
		return 1;
	if (strtoul("0x2a", &end, 0) != 42 || *end)
		return 1;
	if (atoi("17") != 17)
		return 1;
	copy = strndup("abcdef", 3);
	if (!copy || strcmp(copy, "abc"))
		return 1;
	free(copy);
	qsort(values, 4, sizeof(values[0]), compare_int);
	if (values[0] != 1 || values[3] != 4 || *(int *)bsearch(&key, values, 4, sizeof(values[0]), compare_int) != 3)
		return 1;
	puts("PASS stdlib conversion");
	return 0;
}
