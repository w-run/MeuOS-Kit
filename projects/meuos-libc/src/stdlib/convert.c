#include <stdlib.h>

static int
digit_value(int character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'z')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'Z')
		return character - 'A' + 10;
	return 36;
}

unsigned long
strtoul(const char *text, char **end, int base)
{
	unsigned long value = 0;
	int digit;

	while (*text == ' ' || *text == '\t' || *text == '\n')
		++text;
	if (*text == '+')
		++text;
	if (base == 0) {
		base = 10;
		if (text[0] == '0') {
			base = 8;
			if (text[1] == 'x' || text[1] == 'X') { base = 16; text += 2; }
		}
	} else if (base == 16 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
		text += 2;
	}
	while ((digit = digit_value(*text)) < base) {
		value = value * (unsigned long)base + (unsigned long)digit;
		++text;
	}
	if (end)
		*end = (char *)text;
	return value;
}

unsigned long long
strtoull(const char *text, char **end, int base)
{
	unsigned long long value = 0;
	int digit;

	while (*text == ' ' || *text == '\t' || *text == '\n')
		++text;
	if (*text == '+')
		++text;
	if (base == 0) {
		base = 10;
		if (text[0] == '0') {
			base = 8;
			if (text[1] == 'x' || text[1] == 'X') { base = 16; text += 2; }
		}
	} else if (base == 16 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
		text += 2;
	}
	while ((digit = digit_value(*text)) < base) {
#if defined(__i386__)
		extern void meuos_u64_mul_add(unsigned long long *, unsigned, unsigned);
		meuos_u64_mul_add(&value, (unsigned)base, (unsigned)digit);
#else
		value = value * (unsigned long long)base + (unsigned long long)digit;
#endif
		++text;
	}
	if (end)
		*end = (char *)text;
	return value;
}

long
strtol(const char *text, char **end, int base)
{
	int negative = 0;
	while (*text == ' ' || *text == '\t' || *text == '\n')
		++text;
	if (*text == '-') { negative = 1; ++text; }
	else if (*text == '+') ++text;
	return negative ? -(long)strtoul(text, end, base) : (long)strtoul(text, end, base);
}

long long
strtoll(const char *text, char **end, int base)
{
	int negative = 0;

	while (*text == ' ' || *text == '\t' || *text == '\n')
		++text;
	if (*text == '-') { negative = 1; ++text; }
	else if (*text == '+') ++text;
	return negative ? -(long long)strtoull(text, end, base) : (long long)strtoull(text, end, base);
}

/* glibc 兼容别名：glibc 用独立符号暴露 C23 语义的 strtol 家族（base=0 时
 * 解析 0b/0B 二进制前缀等）。本实现的 base 解析对既有调用已足够，直接转发。 */
long
__isoc23_strtol(const char *text, char **end, int base)
{
	return strtol(text, end, base);
}

unsigned long
__isoc23_strtoul(const char *text, char **end, int base)
{
	return strtoul(text, end, base);
}

long long
__isoc23_strtoll(const char *text, char **end, int base)
{
	return strtoll(text, end, base);
}

unsigned long long
__isoc23_strtoull(const char *text, char **end, int base)
{
	return strtoull(text, end, base);
}

#if !defined(__i386__)
double
strtod(const char *text, char **end)
{
	double value = 0.0;
	double fraction = 0.1;
	int negative = 0;
	int exponent = 0;
	int exponent_negative = 0;

	while (*text == ' ' || *text == '\t' || *text == '\n')
		++text;
	if (*text == '-') { negative = 1; ++text; }
	else if (*text == '+') ++text;
	while (*text >= '0' && *text <= '9')
		value = value * 10.0 + (double)(*text++ - '0');
	if (*text == '.') {
		++text;
		while (*text >= '0' && *text <= '9') {
			value += (double)(*text++ - '0') * fraction;
			fraction *= 0.1;
		}
	}
	if (*text == 'e' || *text == 'E') {
		const char *mark = text++;
		if (*text == '-') { exponent_negative = 1; ++text; }
		else if (*text == '+') ++text;
		if (*text < '0' || *text > '9')
			text = mark;
		else
			while (*text >= '0' && *text <= '9')
				exponent = exponent * 10 + *text++ - '0';
	}
	while (exponent-- > 0)
		value = exponent_negative ? value * 0.1 : value * 10.0;
	if (end)
		*end = (char *)text;
	return negative ? -value : value;
}
#endif

int
atoi(const char *text)
{
	return (int)strtol(text, 0, 10);
}

int
abs(int value)
{
	unsigned int u = (unsigned int)value;
	return (int)(value < 0 ? -u : u);
}

long
labs(long value)
{
	unsigned long u = (unsigned long)value;
	return (long)(value < 0 ? -u : u);
}

long long
llabs(long long value)
{
	unsigned long long u = (unsigned long long)value;
	return (long long)(value < 0 ? -u : u);
}
