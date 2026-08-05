/* i386 signed-byte constant-fold gate (defect #22b).
 *
 * A local `signed char x = -56; return x;` is constant-folded by the
 * MIR msimp pass into `sext (i32) (i8)200`, i.e. the low byte 0xC8 of
 * -56 with its sign bit set.  The i386 back end must sign-extend that
 * byte when emitting MOVSX; using the destination dtype (MT_I32) instead
 * of the operand's source width dropped the extension and returned the
 * unsigned low byte 200 instead of -56.
 *
 * The source values are compile-time constants (not runtime params) so
 * the fold path is exercised.  Boundary values (-128, 127, -1, 0) guard
 * the full i8 range; the mixed unsigned case guards that zero-extension
 * is unaffected by the sign-extension fix.
 */
int sb_neg56(void)  { signed char x = -56;   return x; }
int sb_pos120(void) { signed char x = 120;   return x; }
int sb_neg1(void)   { signed char x = -1;    return x; }
int sb_max(void)    { signed char x = 127;   return x; }
int sb_min(void)    { signed char x = -128;  return x; }
int sb_zero(void)   { signed char x = 0;     return x; }
int ub_200(void)    { unsigned char u = 200; return (int)u; }

int main(void) {
    if (sb_neg56() != -56)  return 1;
    if (sb_pos120() != 120) return 2;
    if (sb_neg1() != -1)    return 3;
    if (sb_max() != 127)    return 4;
    if (sb_min() != -128)   return 5;
    if (sb_zero() != 0)     return 6;
    if (ub_200() != 200)    return 7;   /* zero-extend must stay unsigned */
    return 42;
}
