/* wchar_gate.c — wide-character conversion & width gate (region 4).
 *
 * Verifies the previously-absent btowc/wctob/mbrlen/wcwidth family and the
 * byte<->wide conversions, all in the C locale (single byte == wide code).
 * Deliberately avoids C wide-string literals (L".."/L'..'), which the mcc
 * front end does not yet lower (reported separately as an mcc limitation);
 * wide values are constructed as integers. */
#include <wchar.h>
#include <string.h>
#include <stdio.h>

static int fails;

static void
chk(const char *lbl, int cond)
{
	if (!cond) {
		printf("FAIL: %s\n", lbl);
		fails++;
	}
}

int
main(void)
{
	/* btowc: byte -> wide (identity in C locale), EOF -> WEOF */
	chk("btowc A",  btowc('A') == (wint_t)'A');
	chk("btowc EOF", btowc(EOF) == WEOF);
	chk("btowc 0",  btowc(0) == 0);

	/* wctob: wide -> byte, out-of-range -> EOF */
	chk("wctob A",  wctob('A') == 'A');
	chk("wctob WEOF", wctob(WEOF) == EOF);
	chk("wctob big", wctob(0x3ff) == EOF);

	/* mbrlen: C locale each char is 1 byte */
	chk("mbrlen str", mbrlen("abc", 3, NULL) == 1);
	chk("mbrlen empty", mbrlen("", 1, NULL) == 0);
	chk("mbrlen reset", mbrlen(NULL, 0, NULL) == 0);

	/* wcwidth: printable=1, control=0, invalid=-1 */
	chk("wcwidth A",  wcwidth('A') == 1);
	chk("wcwidth 0",  wcwidth(0) == 0);
	chk("wcwidth nl", wcwidth('\n') == 0);
	chk("wcwidth 7f", wcwidth(0x7f) == 0);   /* DEL */
	chk("wcwidth max", wcwidth(0x10ffff) == 1);
	chk("wcwidth neg", wcwidth((wint_t)-1) == -1);

	/* wcrtomb then mbrtowc round trips */
	{
		char mb[4];
		size_t n1 = wcrtomb(mb, 'X', NULL);
		chk("wcrtomb 1byte", n1 == 1);
		chk("wcrtomb val", mb[0] == 'X');
		wchar_t back = 0;
		size_t n2 = mbrtowc(&back, mb, n1, NULL);
		chk("mbrtowc 1", n2 == 1);
		chk("mbrtowc val", back == 'X');
	}

	/* wcstombs/mbstowcs byte conversions (wide arrays built from ints,
	 * since mcc as yet has no wide string literal lowering) */
	{
		char bytes[16];
		wchar_t wsrc[4] = { 'a', 'b', 'c', 0 };
		wchar_t wss[16];
		size_t n = wcstombs(bytes, wsrc, sizeof bytes);
		chk("wcstombs len", n == 3);
		chk("wcstombs val", bytes[0] == 'a' && bytes[2] == 'c');
		n = mbstowcs(wss, "xyz", 16);
		chk("mbstowcs len", n == 3);
		chk("mbstowcs val", wss[0] == (wchar_t)'x' && wss[2] == (wchar_t)'z');
	}

	if (fails) {
		printf("%d wchar FAIL\n", fails);
		return 1;
	}
	printf("PASS wchar\n");
	return 0;
}
