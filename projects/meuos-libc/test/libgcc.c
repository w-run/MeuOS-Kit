#include <stdio.h>
#include <string.h>

/* libgcc ABI soft helpers (declared manually; no <libgcc.h> exists). */
extern long long __divdi3(long long, long long);
extern long long __moddi3(long long, long long);
extern unsigned long long __udivdi3(unsigned long long, unsigned long long);
extern unsigned long long __umoddi3(unsigned long long, unsigned long long);
extern long long __muldi3(long long, long long);
extern unsigned __clzdi2(unsigned long long);
extern unsigned __ctzdi2(unsigned long long);
extern unsigned __popcountdi2(unsigned long long);
extern unsigned __clzsi2(unsigned);
extern unsigned __ctzsi2(unsigned);
extern unsigned long long __bswapdi2(unsigned long long);
extern unsigned __bswapsi2(unsigned);
extern double __floatsidf(int);
extern double __floatdidf(long long);
extern float __floatdisf(long long);
extern long long __fixdfdi(double);

static int
check_signed(long long a, long long b)
{
	/* C division truncates toward zero and remainder takes dividend sign,
	 * matching __divdi3/__moddi3. */
	if (__divdi3(a, b) != a / b)
		return 1;
	if (__moddi3(a, b) != a % b)
		return 2;
	if (__muldi3(a, b) != a * b)
		return 3;
	return 0;
}

int
main(void)
{
	unsigned long long uq, ur;
	int r;

	/* 64-bit unsigned div/mod across edge magnitudes. */
	uq = __udivdi3(0xFFFFFFFFFFFFFF00ull, 7u);
	if (uq != 0xFFFFFFFFFFFFFF00ull / 7u)
		return 10;
	ur = __umoddi3(0xFFFFFFFFFFFFFF00ull, 7u);
	if (ur != 0xFFFFFFFFFFFFFF00ull % 7u)
		return 11;
	if (__udivdi3(1, 3) != 0 || __umoddi3(1, 3) != 1)
		return 12;

	/* signed div/mod incl. trunc-zero + dividend-sign remainder. */
	if ((r = check_signed(100, 7)) || (r = check_signed(-100, 7)) ||
	    (r = check_signed(100, -7)) || (r = check_signed(-100, -7)))
		return 20 + r;
	if (__divdi3(0, 1) != 0 || __moddi3(0, 7) != 0)
		return 25;

	/* bit ops. */
	if (__clzdi2(1ull) != 63)
		return 30;
	if (__clzdi2(0x8000000000000000ull) != 0)
		return 31;
	if (__clzsi2(1u) != 31)
		return 32;
	if (__ctzdi2(0x8000000000000000ull) != 63)
		return 33;
	if (__ctzsi2(0x80000000u) != 31)
		return 34;
	if (__popcountdi2(0xFFFF0000FFFF0000ull) != 32)
		return 35;
	if (__bswapdi2(0x0102030405060708ull) != 0x0807060504030201ull)
		return 36;
	if (__bswapsi2(0x01020304u) != 0x04030201u)
		return 37;

	/* float conversions. */
	if (__floatsidf(12345) != 12345.0)
		return 40;
	if (__floatdidf(-1000000LL) != -1000000.0)
		return 41;
	if (__floatdisf(999) != 999.0f)
		return 42;
	if (__fixdfdi(-3.5) != -3LL)
		return 43;

	puts("PASS libgcc-meuos");
	return 0;
}
