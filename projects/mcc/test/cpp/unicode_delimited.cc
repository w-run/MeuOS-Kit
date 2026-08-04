/* unicode_delimited.cc — C++23 P2290 delimited escape sequences
 * `\u{...}` / `\U{...}` in string and character literals (m++).
 *
 * C++23 allows the universal-character-name escapes to use a braced
 * form: `\u{hex}` / `\U{hex}` with 1..8 hex digits.  The value is
 * encoded in UTF-8:
 *   - \u{48}     -> 'H'  (U+0048)
 *   - \u{1F600}  -> U+1F600 -> F0 9F 98 80
 *   - \U{41}     -> 'A'
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
int
main(void)
{
    /* single-byte escapes */
    const char *s = "\u{48}\u{49}";
    if (s[0] != 'H' || s[1] != 'I' || s[2] != 0) return 1;

    /* U+1F600 (GRINNING FACE) in UTF-8: F0 9F 98 80 */
    const char *e = "\u{1F600}";
    unsigned char b0 = (unsigned char)e[0], b1 = (unsigned char)e[1];
    unsigned char b2 = (unsigned char)e[2], b3 = (unsigned char)e[3];
    if (!(b0 == 0xF0 && b1 == 0x9F && b2 == 0x98 && b3 == 0x80)) return 2;

    /* uppercase \U{...} form */
    const char *u = "\U{41}";
    if (u[0] != 'A') return 3;

    /* delimited escape in a character literal */
    char c = '\u{5A}';
    if (c != 'Z') return 4;

    return 0;
}
