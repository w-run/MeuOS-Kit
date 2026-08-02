#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

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
	if (llabs(LLONG_MAX) != LLONG_MAX || llabs(-9223372036854775807LL) != 9223372036854775807LL)
		return 1;
	{
		div_t d = div(7, 2);
		if (d.quot != 3 || d.rem != 1)
			return 1;
		d = div(-7, 2);
		if (d.quot != -3 || d.rem != -1)
			return 1;
		d = div(7, -2);
		if (d.quot != -3 || d.rem != 1)
			return 1;
		ldiv_t l = ldiv(100, 7);
		if (l.quot != 14 || l.rem != 2)
			return 1;
		l = ldiv(-100, 7);
		if (l.quot != -14 || l.rem != -2)
			return 1;
	}
	{
		int i, r1, r2;
		srand(1234);
		r1 = rand();
		if (r1 < 0 || r1 > RAND_MAX || RAND_MAX < 32767)
			return 1;
		for (i = 0; i < 100; i++)
			if (rand() < 0 || rand() > RAND_MAX)
				return 1;
		srand(99);
		rand();
		srand(99);
		r2 = rand();
		if (r1 == r2)	/* different seeds should diverge */
			return 1;
		srand(1234);
		if (rand() != r1)
			return 1;
	}
	if (getppid() <= 0)
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
