/* string/compare.c — 字符串比较和搜索函数 */

#include <string.h>

size_t
strspn(const char *s, const char *accept)
{
	size_t n = 0;
	const char *p;
	for (; *s; ++s) {
		for (p = accept; *p; ++p)
			if (*s == *p) goto matched;
		break;
matched: ++n;
	}
	return n;
}

size_t
strcspn(const char *s, const char *reject)
{
	size_t n = 0;
	const char *p;
	for (; *s; ++s) {
		for (p = reject; *p; ++p)
			if (*s == *p) return n;
		++n;
	}
	return n;
}

int
strcasecmp(const char *s1, const char *s2)
{
	for (; *s1 && *s2; ++s1, ++s2) {
		int c1 = *s1, c2 = *s2;
		if (c1 >= 'A' && c1 <= 'Z') c1 += 'a' - 'A';
		if (c2 >= 'A' && c2 <= 'Z') c2 += 'a' - 'A';
		if (c1 != c2) return c1 - c2;
	}
	return (unsigned char)*s1 - (unsigned char)*s2;
}

int
strncasecmp(const char *s1, const char *s2, size_t n)
{
	if (n == 0) return 0;
	for (; *s1 && *s2 && n > 0; ++s1, ++s2, --n) {
		int c1 = *s1, c2 = *s2;
		if (c1 >= 'A' && c1 <= 'Z') c1 += 'a' - 'A';
		if (c2 >= 'A' && c2 <= 'Z') c2 += 'a' - 'A';
		if (c1 != c2) return c1 - c2;
	}
	if (n == 0) return 0;
	return (unsigned char)*s1 - (unsigned char)*s2;
}
