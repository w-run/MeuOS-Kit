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

static int
hexdigit_value(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* Parse a C99 hexadecimal floating constant "0x1.8p3" (6.4.4.2).  Returns
 * nonzero if text is a hex float (mantissa + mandatory p/P exponent),
 * filling *value and *end.  The mantissa is accumulated exactly (up to 15
 * hex digits), then scaled by 2^exp.  "0x..." without p/P is an integer
 * and returns 0 so the caller treats it as such. */
static int
parse_hex_float(const char *text, double *value, const char **end)
{
	const char *p = text;
	unsigned long long mant = 0;
	int ndig = 0;
	int exp = 0, exp_sign = 0;
	double frac = 1.0;

	if (p[0] != '0' || (p[1] != 'x' && p[1] != 'X'))
		return 0;
	p += 2;

	while (hexdigit_value(*p) >= 0) {
		if (ndig < 15) {
			mant = mant * 16 + (unsigned)hexdigit_value(*p);
			++ndig;
		} else {
			exp += 4;   /* extra integer digits only shift the exponent */
		}
		++p;
	}
	if (*p == '.') {
		++p;
		while (hexdigit_value(*p) >= 0) {
			if (ndig < 15) {
				mant = mant * 16 + (unsigned)hexdigit_value(*p);
				++ndig;
				frac *= 16.0;
			} else {
				exp -= 4;   /* extra fractional digits shift the exponent */
			}
			++p;
		}
	}
	if (ndig == 0)
		return 0;   /* "0x" or "0x." alone: not a number at all */
	if (*p != 'p' && *p != 'P')
		return 0;   /* no binary exponent: it's an integer, not a float */
	++p;
	if (*p == '-') { exp_sign = -1; ++p; }
	else if (*p == '+') ++p;
	if (*p < '0' || *p > '9')
		return 0;   /* "0x1p" with no exponent: invalid hex float */
	while (*p >= '0' && *p <= '9')
		exp = exp * 10 + *p++ - '0';
	if (exp_sign < 0)
		exp = -exp;

	/* value = mant / frac * 2^exp, scaling in exact binary steps */
	double v = (double)mant / frac;
	if (exp > 0)
		while (exp-- > 0) v *= 2.0;
	else if (exp < 0)
		while (exp++ < 0) v *= 0.5;

	*value = v;
	*end = p;
	return 1;
}

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

	/* C99 hexadecimal floating constant: "0x"/"0X" + hex digits + p/P
	 * binary exponent (6.4.4.2).  Without p/P it's an integer constant
	 * and the decimal path below would mis-parse it, so hex ints are
	 * handled here too (0x... consumed, end points past the digits). */
	if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
		const char *after;
		if (parse_hex_float(text, &value, &after)) {
			if (end)
				*end = (char *)after;
			return negative ? -value : value;
		}
		/* plain hex integer: parse via strtoull so "0x1.8" stops at '.'
		 * just like glibc (value 1, end at '.'), and "0x10" is 16. */
		value = (double)strtoull(text + 2, (char **)&after, 16);
		if (end)
			*end = (char *)after;
		return negative ? -value : value;
	}

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

/* C99 7.20.1.3: strtof / strtold narrow the same decimal/hex parsing.
 * strtold needs 80-bit long double which mcc does not support yet, so it is
 * deferred.  strtof reuses strtod and rounds to float; for the vast majority
 * of inputs the double-rounding result equals glibc's directly-rounded value.
 * The cases that differ are float halfway ties at the limit of precision. */
float
strtof(const char *text, char **end)
{
	return (float)strtod(text, end);
}

int
atoi(const char *text)
{
	return (int)strtol(text, 0, 10);
}

double
atof(const char *text)
{
	return strtod(text, 0);
}

long
atol(const char *text)
{
	return strtol(text, 0, 10);
}

long long
atoll(const char *text)
{
	return strtoll(text, 0, 10);
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
	/* 避免 64 位有符号比较：mcc 的 i386 后端不支持 Kl flagislt
	 *（见 src/target/i386/i386_emit.c "Kl op %s not yet supported"），
	 * 原 `value < 0 ? -u : u` 触发崩溃。改用符号掩码算术求绝对值；
	 * 对 INT64_MIN 的溢出行为与原三元版一致（UB，位结果相同）。 */
	unsigned long long u = (unsigned long long)value;
	unsigned long long s = 0 - (u >> 63);	/* 全 1（负）或全 0（正） */
	return (long long)((u ^ s) - s);
}

/* ---- C99 7.20.2: rand/srand (deterministic LCG, glibc-compatible
 *      constants so callers porting seed sequences see the same stream). */
static unsigned long __meuos_rand_state = 1;

int
rand(void)
{
	__meuos_rand_state = __meuos_rand_state * 1103515245UL + 12345UL;
	return (int)((__meuos_rand_state >> 16) & 0x7fff);
}

void
srand(unsigned int seed)
{
	__meuos_rand_state = (unsigned long)seed;
}

/* ---- C99 7.20.6.2: div / ldiv ---- */
div_t
div(int numer, int denom)
{
	div_t result;
	result.quot = numer / denom;
	result.rem = numer % denom;
	return result;
}

ldiv_t
ldiv(long numer, long denom)
{
	ldiv_t result;
	result.quot = numer / denom;
	result.rem = numer % denom;
	return result;
}
