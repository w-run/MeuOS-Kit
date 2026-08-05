/* C wide string/char literals (C11 §6.4.5, §6.4.4.4).
 *
 * The C frontend's IR emitter previously aborted on wide string
 * initializers ("wide string initializer not yet supported").  Guard that a
 * wide string literal (L"...") materialises each wchar_t element and that a
 * wide char literal (L'x') is a scalar wchar_t value, on LP64 (wchar_t == 4
 * bytes).  Returns 0 on success, nonzero on failure. */
typedef int wchar_t;   /* LP64 Linux: wchar_t is a 32-bit signed int */

int
main(void)
{
	wchar_t ws[] = L"ab";      /* { 'a', 'b', 0 } — 3 elems * 4 = 12 bytes */
	wchar_t ws6[6] = L"xy";    /* partial init: "xy\0" then zero-fill */
	wchar_t ch = L'Q';
	int i;

	if (sizeof(wchar_t) != 4)
		return 1;
	if (sizeof(ws) != 12)
		return 2;
	if (ws[0] != L'a' || ws[1] != L'b' || ws[2] != 0)
		return 3;
	for (i = 0; i < 6; i++) {
		int want = i < 3 ? (i == 0 ? L'x' : i == 1 ? L'y' : 0) : 0;
		if (ws6[i] != want)
			return 4;
	}
	if (ch != L'Q')
		return 5;

	return 0;
}
