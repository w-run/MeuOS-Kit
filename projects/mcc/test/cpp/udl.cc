// udl.cc -- C++11 user-defined literals (operator""_suffix)
//
// Supported forms: integer, floating-point, and string literals.
// (mcc has no long double backend, so the float form uses double here;
//  the standard long double overload would also parse/register fine.)

unsigned mystrlen(const char *s)
{
	unsigned n = 0;
	while (s[n])
		++n;
	return n;
}

int mystrcmp(const char *a, const char *b)
{
	while (*a && *a == *b) {
		++a;
		++b;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

// 1. floating-point UDL
double operator""_km(double v) { return v * 1000; }

// 2. integer UDL
unsigned long long operator""_km2(unsigned long long v) { return v * 2; }

// 3. string UDL: (const char*, size_t)
const char *operator""_s(const char *str, unsigned long n) { return str + n; }

// 4. raw-string form: single const char* parameter
unsigned operator""_len(const char *str) { return mystrlen(str); }

int main(void)
{
	// float literal suffix
	if ((int)1.5_km != 1500)
		return 1;
	// integer literal suffix
	if ((int)3_km2 != 6)
		return 2;
	// string literal suffix with length parameter
	if (mystrcmp("hi"_s, "") != 0)
		return 3;
	// raw string form
	if ("hello"_len != 5)
		return 4;
	// float UDL on a value with exponent suffix
	if ((int)2.0_km != 2000)
		return 5;
	// UDL use in an arithmetic expression
	if ((int)(1.25_km + 1.25_km) != 2500)
		return 6;
	// a plain literal without a suffix is untouched
	if ((int)2.5 != 2)
		return 7;
	return 0;
}
