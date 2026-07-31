#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* glibc 内部符号：不在公共头文件中声明 */
long __isoc23_strtol(const char *, char **, int);
unsigned long __isoc23_strtoul(const char *, char **, int);
long long __isoc23_strtoll(const char *, char **, int);
unsigned long long __isoc23_strtoull(const char *, char **, int);

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
	if (__isoc23_strtol("123", NULL, 10) != 123 ||
	    __isoc23_strtoul("42", NULL, 10) != 42 ||
	    __isoc23_strtoll("-7", NULL, 10) != -7 ||
	    __isoc23_strtoull("0x1f", NULL, 0) != 31)
		return 1;
	if (setenv("MEUOS_LIBC_TEST", "one", 0) != 0)
		return 1;
	if (!getenv("MEUOS_LIBC_TEST") || strcmp(getenv("MEUOS_LIBC_TEST"), "one"))
		return 1;
	if (setenv("MEUOS_LIBC_TEST", "two", 0) != 0 || strcmp(getenv("MEUOS_LIBC_TEST"), "one"))
		return 1;
	if (setenv("MEUOS_LIBC_TEST", "two", 1) != 0 || strcmp(getenv("MEUOS_LIBC_TEST"), "two"))
		return 1;
	if (unsetenv("MEUOS_LIBC_TEST") != 0 || getenv("MEUOS_LIBC_TEST"))
		return 1;
	if (setenv("BAD=NAME", "x", 0) == 0 || unsetenv("BAD=NAME") == 0)
		return 1;
	puts("PASS stdlib conversion");
	return 0;
}
