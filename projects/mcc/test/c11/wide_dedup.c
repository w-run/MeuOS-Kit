/* C wide-string de-duplication (C11 §6.4.5).
 *
 * Two different wide literals in the same TU must materialise as distinct
 * string objects.  The C frontend's interning map compared wide literals by
 * element count (memcmp over sizeof(wchar_t) bytes), so L"abc" and L"abd"
 * shared the leading bytes and were wrongly merged into one object, leaving
 * the second pointer pointing at the first literal's data.
 *
 * Returns 0 on success, nonzero on the first failure. */
typedef int wchar_t;   /* LP64 Linux: wchar_t is a 32-bit signed int */

int
main(void)
{
	const wchar_t *wa = L"abc";
	const wchar_t *wb = L"abd";   /* must NOT alias wa */
	const wchar_t *wc = L"abc";   /* may share wa's storage (same content) */
	int i;

	if (sizeof(wchar_t) != 4)
		return 1;
	/* wa == L"abc" */
	for (i = 0; i < 3; i++) {
		int want = i == 0 ? L'a' : i == 1 ? L'b' : L'c';
		if (wa[i] != want)
			return 2;
	}
	if (wa[3] != 0)
		return 3;
	/* wb == L"abd" — the regression merged wb onto wa ("abc"). */
	for (i = 0; i < 3; i++) {
		int want = i == 0 ? L'a' : i == 1 ? L'b' : L'd';
		if (wb[i] != want)
			return 4;
	}
	if (wb[3] != 0)
		return 5;
	/* wa and wb must hold different content at the third element */
	if (wa[2] != L'c' || wb[2] != L'd')
		return 6;
	/* wc (identical content) reads back as L"abc" */
	for (i = 0; i < 3; i++) {
		int want = i == 0 ? L'a' : i == 1 ? L'b' : L'c';
		if (wc[i] != want)
			return 7;
	}

	return 0;
}
