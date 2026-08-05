/* Regression: wide-string-literal dedup must compare the full byte
 * contents, not just the first element.
 *
 * stringdecl keys the string-literal dedup map on (data, size) where
 * `size` is the code-unit count.  For a wide literal the buffer is
 * size*width bytes, so keying on `size` alone hashed/compared only the
 * first element's bytes: L"abc" and L"abd" both start with 'a' and were
 * wrongly merged into one .Lstring object (both globals pointed at the
 * first one).  The fix multiplies the element count by the element width
 * so the whole payload participates in the key.
 *
 * This test asserts, at runtime, that two *different* wide literals yield
 * distinct objects while two *identical* ones share a single object.
 *
 * NOTE: `wchar_t` is declared locally (matching the LP64 target's
 * 32-bit wchar) instead of via <wchar.h>, which pulls in the meuos libc
 * <stdlib.h> where wchar_t isn't yet defined before a use site (an
 * independent meuos-libc header gap).  The L prefix element type is the
 * platform wchar_t, i.e. 4 bytes on x86_64 Linux.
 */
extern int printf(const char *, ...);

typedef int wchar_t;

const wchar_t *g_a = L"abc";
const wchar_t *g_b = L"abd";   /* differs in the 3rd char */
const wchar_t *g_c = L"abc";   /* identical to g_a -> must share */

int main(void) {
    /* Distinct contents must NOT share storage. */
    if (g_a == g_b) { printf("FAIL: L\"abc\" == L\"abd\"\n"); return 1; }
    /* Identical contents may (and do) share one object. */
    if (g_a != g_c) { printf("FAIL: L\"abc\" != L\"abc\"\n"); return 2; }
    /* The payload of the second literal must be intact. */
    if (g_b[0] != L'a' || g_b[1] != L'b' || g_b[2] != L'd' || g_b[3] != 0) {
        printf("FAIL: L\"abd\" payload corrupted\n"); return 3;
    }
    if (g_a[2] != L'c' || g_a[3] != 0) { printf("FAIL: L\"abc\" payload\n"); return 4; }

    /* Function-local wide literals across separate expressions. */
    const wchar_t *l1 = L"xyz";
    const wchar_t *l2 = L"xyw";
    if (l1 == l2) { printf("FAIL: local L differ\n"); return 5; }
    return 0;
}
