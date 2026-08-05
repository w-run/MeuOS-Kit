/* i386 byte/half-narrowing return gate (defect #22a).
 *
 * i386_memit.c's MMOP_MOVZX case for an i8/i16 -> i32 source fell through
 * into the MMOP_LEA case and emitted `leal 0,%eax`, whose address-0
 * semantics zero eax and clobbered the narrowed value being returned
 * (e.g. `(unsigned char)int` returned 0 instead of the low byte; a regress
 * showed f(200)==200 returning 0, and f((unsigned char)0x78)==0x78).
 *
 * The feed functions are deliberately non-`static` and take a runtime value
 * so the i32 -> i8/i16 narrowing result is actually materialised at the
 * call boundary rather than folded at compile time.
 */
int ub(unsigned v) { return (unsigned char)v; }
int sb(signed v)   { return (signed char)v; }
int ha(unsigned v) { return (unsigned short)v; }

int main(void) {
    if (ub(200) != 200) return 1;        /* 200 keeps low byte */
    if (ub(0x1234) != 0x34) return 2;    /* 0x34 = low byte */
    if (sb(0x80) != (signed char)0x80) return 3;  /* sign-aware */
    if (ha(0x123456) != 0x3456) return 4;         /* low half */
    return 42;
}
