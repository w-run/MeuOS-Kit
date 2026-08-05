/* Regression: constant folding / runtime narrowing conversions must truncate
 * (chibicc B-class bug).  A narrowing cast (int -> char/short, long long ->
 * short, ...) must mask the source to the destination width and, for signed
 * destinations, sign-extend the truncated result — not pass the wide value
 * through untouched.
 *
 * Repro 1: (_Bool)(char)256   -> 0   (char)256 truncates to 0, then _Bool is 0
 * Repro 2: (short)8590066177  -> 513 (low 16 bits of 0x1FF400001)
 */
extern int puts(const char *);

_Bool t1(void) { return (_Bool)(char)256; }
short t2(void) { return (short)8590066177; }

int main(void) {
    if (t1() != 0)   { puts("FAIL: (_Bool)(char)256"); return 1; }
    if (t2() != 513) { puts("FAIL: (short)8590066177"); return 1; }

    /* unsigned narrowing truncates to low bits */
    unsigned char uc = (unsigned char)511;   /* 511 & 0xFF = 255 */
    if (uc != 255) { puts("FAIL: unsigned char truncate"); return 1; }

    unsigned u = 0x10000;
    short s = (short)u;                       /* 0x10000 & 0xFFFF = 0 */
    if (s != 0) { puts("FAIL: short truncate unsigned"); return 1; }

    /* signed narrowing must sign-extend the low bits */
    signed char sc = (signed char)(-1);       /* 0xFF -> -1 */
    if (sc != -1) { puts("FAIL: signed char sign-extend"); return 1; }
    signed char sc2 = (signed char)0x100;     /* 0x100 & 0xFF = 0 */
    if (sc2 != 0) { puts("FAIL: signed char truncate high"); return 1; }

    /* long long -> short (same as t2, exercised as a statement) */
    long long big = 8590066177LL;
    short s2 = (short)big;
    if (s2 != 513) { puts("FAIL: short truncate from long long"); return 1; }

    /* int -> short */
    int i = 0x10001;
    short s3 = (short)i;                      /* 0x10001 & 0xFFFF = 1 */
    if (s3 != 1) { puts("FAIL: int -> short truncate"); return 1; }

    return 0;
}
