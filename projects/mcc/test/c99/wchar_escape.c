/* Regression: a wide/char character constant whose hexadecimal escape
 * exceeds the destination type range is implementation-defined
 * (C11 6.4.4.4p10); mcc must wrap to the type width instead of
 * rejecting.  chibicc keeps L'\xffffffff' as the full 32-bit value.
 */
extern int puts(const char *);

int main(void) {
    if ((int)L'\xffffffff' != -1) { puts("FAIL: L'\\xffffffff'"); return 1; }
    if ((int)L'\x7fffffff' != 2147483647) { puts("FAIL: L'\\x7fffffff'"); return 2; }
    /* u-prefixed (16-bit) constant wraps to low 16 bits */
    if ((int)u'\xffff' != 65535) { puts("FAIL: u'\\xffff'"); return 3; }
    /* narrow character constant wraps to 8 bits */
    if ((int)'\xff' != (signed char)0xff) { puts("FAIL: '\\xff'"); return 4; }
    /* in-range values unaffected */
    if ((int)L'\x41' != 0x41) { puts("FAIL: L'A'"); return 5; }
    return 0;
}
