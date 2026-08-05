/* wctype_gate.c — wctype / iswctype / wctrans / towctrans gate + real
 * wide literals.
 *
 * Exercise the C11 7.29.2.2 (iswctype/wctype) and 7.29.6.4 (towctrans/
 * wctrans) functions now implemented in meuos-libc.  Uses genuine wide
 * string literals L".."/L'..' (mcc lowers them since the 2026-08 wchar
 * front-end work), so this also serves as a real-wide-literal gate.
 */
#include <wchar.h>
#include <wctype.h>
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
	/* --- wctype(): known classes resolve to non-zero, unknown to 0 --- */
	chk("wctype alpha",   wctype("alpha")   != 0);
	chk("wctype digit",   wctype("digit")   != 0);
	chk("wctype blank",   wctype("blank")   != 0);
	chk("wctype xdigit",  wctype("xdigit")  != 0);
	chk("wctype alnum",   wctype("alnum")   != 0);
	chk("wctype upper",   wctype("upper")   != 0);
	chk("wctype lower",   wctype("lower")   != 0);
	chk("wctype space",   wctype("space")   != 0);
	chk("wctype print",   wctype("print")   != 0);
	chk("wctype graph",   wctype("graph")   != 0);
	chk("wctype punct",   wctype("punct")   != 0);
	chk("wctype cntrl",   wctype("cntrl")   != 0);
	chk("wctype unknown", wctype("notaclass") == 0);

	/* --- iswctype(): classify via descriptors --- */
	chk("iswctype A alpha",  iswctype(L'A', wctype("alpha")));
	chk("iswctype A upper",  iswctype(L'A', wctype("upper")));
	chk("iswctype a !upper", !iswctype(L'a', wctype("upper")));
	chk("iswctype 7 digit",  iswctype(L'7', wctype("digit")));
	chk("iswctype 7 !alpha", !iswctype(L'7', wctype("alpha")));
	chk("iswctype sp space", iswctype(L' ', wctype("space")));
	chk("iswctype sp blank", iswctype(L' ', wctype("blank")));
	chk("iswctype A !space", !iswctype(L'A', wctype("space")));
	/* descriptor 0 (unknown property) -> always false */
	chk("iswctype zero",     !iswctype(L'A', (wctype_t)0));
	/* out-of-range code -> false in C locale */
	chk("iswctype oob",      !iswctype((wint_t)0x1ff, wctype("alpha")));

	/* --- wctrans(): known mappings resolve, unknown -> 0 --- */
	chk("wctrans tolower", wctrans("tolower") == 0);
	chk("wctrans toupper", wctrans("toupper") == 1);
	chk("wctrans unknown", wctrans("nosuch")  == 0);

	/* --- towctrans(): apply mappings --- */
	chk("towctrans lower", towctrans(L'A', wctrans("tolower")) == L'a');
	chk("towctrans upper", towctrans(L'a', wctrans("toupper")) == L'A');
	chk("towctrans unk",   towctrans(L'x', (wctrans_t)99) == L'x');

	/* --- real wide string literal + wcslen --- */
	{
		wchar_t s[] = L"h\x00e9llo";   /* héll (é = U+00E9) + o */
		chk("wcslen literal", wcslen(s) == 5);
		chk("wide lit cmd",   s[1] == L'\xe9');
	}
	{
		/* iswalpha on a genuine wide literal character */
		wchar_t c = L'Z';
		chk("iswalpha literal", iswalpha(c));
	}

	if (fails) {
		printf("%d wctype FAIL\n", fails);
		return 1;
	}
	printf("PASS wctype\n");
	return 0;
}
