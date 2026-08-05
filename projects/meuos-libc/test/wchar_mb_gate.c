/* wchar_mb_gate.c — UTF-8 multibyte<->wide conversion edge cases.
 *
 * Exercises the C11 mbrtowc/wcrtomb/mbrlen/mbsinit family with real UTF-8
 * byte sequences and streaming conversion through an mbstate_t.  Wide values
 * are constructed as integers (the mcc wide-literal gate lives in a separate
 * file); here we focus on byte-level correctness of the conversion engine. */
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
	/* --- single-shot UTF-8 decode (mbtowc) --- */
	{
		wchar_t wc;
		/* U+00E9 (é) = C3 A9 */
		chk("mbtowc 2byte", mbtowc(&wc, "\xc3\xa9", 2) == 2 && wc == 0x00e9);
		/* U+4E2D (中) = E4 B8 AD */
		chk("mbtowc 3byte", mbtowc(&wc, "\xe4\xb8\xad", 3) == 3 && wc == 0x4e2d);
		/* U+1F600 (😀) = F0 9F 98 80 */
		chk("mbtowc 4byte", mbtowc(&wc, "\xf0\x9f\x98\x80", 4) == 4 && wc == 0x1f600);
		/* invalid lead byte */
		chk("mbtowc bad", mbtowc(&wc, "\xff", 1) == -1);
		/* truncated */
		chk("mbtowc trunc", mbtowc(&wc, "\xe4\xb8", 2) == -1);
	}

	/* --- single-shot UTF-8 encode (wctomb) --- */
	{
		char buf[8];
		chk("wctomb 2byte", wctomb(buf, 0x00e9) == 2 &&
		    (unsigned char)buf[0] == 0xc3 && (unsigned char)buf[1] == 0xa9);
		chk("wctomb 3byte", wctomb(buf, 0x4e2d) == 3 &&
		    (unsigned char)buf[0] == 0xe4 && (unsigned char)buf[1] == 0xb8 &&
		    (unsigned char)buf[2] == 0xad);
		chk("wctomb 4byte", wctomb(buf, 0x1f600) == 4 &&
		    (unsigned char)buf[0] == 0xf0 && (unsigned char)buf[1] == 0x9f &&
		    (unsigned char)buf[2] == 0x98 && (unsigned char)buf[3] == 0x80);
	}

	/* --- round trip through mbrtowc/wcrtomb --- */
	{
		const char *u = "\xe4\xb8\xad\xf0\x9f\x98\x80"; /* 中😀 */
		wchar_t out[4];
		size_t got = mbstowcs(out, u, 4);
		chk("mbstowcs 2", got == 2 && out[0] == 0x4e2d && out[1] == 0x1f600);
		char back[16];
		size_t n = wcstombs(back, out, 16);
		chk("wcstombs len", n == 7);
		chk("wcstombs val", memcmp(back, u, 7) == 0);
	}

	/* --- streaming conversion via mbstate_t --- */
	{
		mbstate_t st;
		memset(&st, 0, sizeof st);
		chk("mbsinit init", mbsinit(&st) != 0);

		const char *p = "\xf0\x9f\x98\x80"; /* 4-byte, fed 1 byte at a time */
		wchar_t wc = 0;
		size_t r;
		r = mbrtowc(&wc, p + 0, 1, &st);
		chk("mbrtowc partial1", r == (size_t)-2 && mbsinit(&st) == 0);
		r = mbrtowc(NULL, p + 1, 1, &st);
		chk("mbrtowc partial2", r == (size_t)-2);
		r = mbrtowc(NULL, p + 2, 1, &st);
		chk("mbrtowc partial3", r == (size_t)-2);
		r = mbrtowc(&wc, p + 3, 1, &st);
		chk("mbrtowc done", r == 4 && wc == 0x1f600);
		chk("mbsinit after", mbsinit(&st) != 0);
	}

	/* --- mbrlen: incomplete returns -2, complete returns length --- */
	{
		mbstate_t st;
		memset(&st, 0, sizeof st);
		chk("mbrlen inc", mbrlen("\xe4\xb8", 2, &st) == (size_t)-2);
		chk("mbrlen full", mbrlen("\xe4\xb8\xad", 3, NULL) == 3);
		chk("mbrlen nul", mbrlen("", 1, NULL) == 0);
	}

	/* --- invalid continuation byte -> -1 --- */
	{
		const char *bad = "\xe4\x20\xad"; /* 0x20 is not a continuation byte */
		wchar_t wc;
		chk("mbrtowc badcont", mbrtowc(&wc, bad, 3, NULL) == (size_t)-1);
	}

	/* --- wcrtomb query with NULL --- */
	{
		chk("wcrtomb query", wcrtomb(NULL, 0x41, NULL) >= 1);
	}

	/* --- wcrtomb then re-decode byte count --- */
	{
		char buf[8];
		size_t n = wcrtomb(buf, 0x4e2d, NULL);
		chk("wcrtomb 3", n == 3);
		wchar_t wc;
		chk("re-decode", mbrtowc(&wc, buf, n, NULL) == n && wc == 0x4e2d);
	}

	if (fails) {
		printf("%d wchar_mb FAIL\n", fails);
		return 1;
	}
	printf("PASS wchar_mb\n");
	return 0;
}
