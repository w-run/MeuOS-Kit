/* wchar_lit_gate.c — true wide-string literal (L"..") coverage.
 *
 * Once mcc lowered wide literals into the object data, a gate can exercise
 * the wchar family against real L".."/L'..' constants instead of hand-built
 * integer arrays.  Verifies the literal decoding, the string/classification
 * functions, and the byte<->wide conversions driven by wide literals. */
#include <wchar.h>
#include <string.h>
#include <stdio.h>

static int fails;

static void chk(const char *lbl, int cond)
{
	if (!cond) {
		printf("FAIL: %s\n", lbl);
		fails++;
	}
}

int main(void)
{
	/* --- wide string literal decoding + wcslen --- */
	{
		static const wchar_t *s = L"abc";
		chk("lstr len", wcslen(s) == 3);
		chk("lstr s0", s[0] == L'a');
		chk("lstr s1", s[1] == L'b');
		chk("lstr s2", s[2] == L'c');
	}

	/* --- multi-byte BMP + astral wide literal --- */
	{
		static const wchar_t ws[] = L"中😀";
		chk("lwcs len", wcslen(ws) == 2);
		chk("lwcs bmp", ws[0] == 0x4e2d);      /* 中 */
		chk("lwcs astral", ws[1] == 0x1f600);  /* 😀 */
	}

	/* --- wide char literal constant --- */
	chk("lchar A", L'A' == 0x41);
	chk("lchar cjk", L'中' == 0x4e2d);

	/* --- wide string functions on literals --- */
	{
		static const wchar_t *h = L"hello world";
		chk("wcsstr",  wcsstr(h, L"world") == h + 6);
		chk("wcschr",  wcschr(h, L'o') == h + 4);
		chk("wcsrchr", wcsrchr(h, L'o') == h + 7);
		chk("wcscmp eq", wcscmp(L"abc", L"abc") == 0);
		chk("wcscmp lt", wcscmp(L"abc", L"abd") < 0);
		chk("wcspbrk none", wcspbrk(L"123", L"ab") == NULL);
	}
	{
		static const wchar_t *digits = L"12345";
		chk("wcspbrk hit", wcspbrk(digits, L"a3") == digits + 2);
		chk("wcsspn", wcsspn(L"11123", L"12") == 4); /* "1112" ∈ {1,2} */
		chk("wcscspn", wcscspn(L"abc", L"xyc") == 2);
	}

	/* --- wide char literal / conversions --- */
	{
		chk("btowc lc", btowc((int)L'A') == L'A');
		char mb[8];
		size_t n = wcrtomb(mb, L'中', NULL);
		chk("wcrtomb 3", n == 3 &&
		    (unsigned char)mb[0] == 0xe4 && (unsigned char)mb[2] == 0xad);
	}

	/* --- wcwidth on wide literals --- */
	chk("lwidth ascii", wcwidth(L'A') == 1);
	chk("lwidth cjk", wcwidth(L'中') == 2);
	chk("lwidth astral", wcwidth(L'😀') == 2);

	if (fails) {
		printf("%d wchar_lit FAIL\n", fails);
		return 1;
	}
	printf("PASS wchar_lit\n");
	return 0;
}
