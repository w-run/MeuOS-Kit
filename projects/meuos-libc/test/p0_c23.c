/* p0_c23.c — ISO C23 三头 + timespec_getres 合约 gate.
 *
 * 验证 <stdbit.h> / <stdckdint.h> / <stdcountof.h> / timespec_getres
 * 可正常编译调用。 */
#include <stdio.h>
#include <string.h>
#include <stdbit.h>
#include <stdckdint.h>
#include <stdcountof.h>
#include <time.h>

int main(void)
{
	int err = 0;

	/* ---- stdbit.h ---- */
	{
		unsigned x = 0x80000000u;
		unsigned lz = stdc_leading_zeros(x);
		if (lz != 0) { printf("FAIL stdc_leading_zeros(0x80000000)=%u\n", lz); err = 1; }

		unsigned char uc = 0x80u;
		unsigned lz8 = stdc_leading_zeros(uc);
		if (lz8 != 0) { printf("FAIL stdc_leading_zeros(uc0x80)=%u\n", lz8); err = 1; }

		unsigned short us = 0x0001u;
		unsigned tz = stdc_trailing_zeros(us);
		if (tz != 0) { printf("FAIL stdc_trailing_zeros(1)=%u\n", tz); err = 1; }

		unsigned o = stdc_count_ones(0x00000000u);
		if (o != 0) { printf("FAIL stdc_count_ones(0)=%u\n", o); err = 1; }

		unsigned long long pw = stdc_bit_width(0ull);
		if (pw != 0) { printf("FAIL stdc_bit_width(0)=%u\n", pw); err = 1; }
		unsigned long long pw2 = stdc_bit_width(0x1000000000000000ull);
		if (pw2 != 61) { printf("FAIL stdc_bit_width(0x1000...)=%u\n", pw2); err = 1; }

		if (!stdc_has_single_bit(0x8000u)) { puts("FAIL stdc_has_single_bit(0x8000)"); err = 1; }
		if (stdc_has_single_bit(0x8001u)) { puts("FAIL stdc_has_single_bit(0x8001)"); err = 1; }

		unsigned long fl = stdc_bit_floor(0x1234ul);
		if (fl != 0x1000ul) { printf("FAIL stdc_bit_floor(0x1234)=%lu\n", fl); err = 1; }

		unsigned long long cl = stdc_bit_ceil(0x1234ull);
		if (cl != 0x2000ull) { printf("FAIL stdc_bit_ceil(0x1234)=%llu\n", cl); err = 1; }
	}

	/* ---- stdckdint.h ---- */
	{
		int r;
		if (ckd_add(r, 100, 200) || r != 300)
			{ puts("FAIL ckd_add normal"); err = 1; }
		if (!ckd_add(r, INT_MAX, 1) || r != INT_MIN)
			{ puts("FAIL ckd_add overflow"); err = 1; }

		unsigned ur;
		if (ckd_sub(ur, 5u, 3u) || ur != 2u)
			{ puts("FAIL ckd_sub normal"); err = 1; }
		if (!ckd_sub(ur, 0u, 1u))
			{ puts("FAIL ckd_sub underflow"); err = 1; }

		long long lr;
		if (ckd_mul(lr, 1000000LL, 2000000LL) || lr != 2000000000000LL)
			{ puts("FAIL ckd_mul normal"); err = 1; }
	}

	/* ---- stdcountof.h ---- */
	{
		int arr[16];
		if (countof(arr) != 16) { puts("FAIL countof"); err = 1; }
	}

	/* ---- timespec_getres ---- */
	{
		struct timespec ts;
		int ret = timespec_getres(&ts, TIME_UTC);
		if (ret != 0) { puts("FAIL timespec_getres"); err = 1; }
	}

	if (err) return 1;
	puts("PASS C23 三头 + timespec_getres");
	return 0;
}