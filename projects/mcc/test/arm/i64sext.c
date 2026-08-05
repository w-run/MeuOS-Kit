/* arm/i386 i64 sign/zero-extension compile gate.
 *
 * `long long y = 5;` lowers to an i64 sext of a 32-bit constant.  On the
 * split-register 32-bit backends a 64-bit value lives in a slot pair, and
 * the pre-fix emitter only materialised the LOW half — the high half of a
 * sign-extended/zero-extended constant was never written, so consumers read
 * uninitialised frame memory.  Compile-level: assert both halves of an i64
 * sext/zext result are stored (a movw imm and a subsequent separate store).
 */
long long sext_const(void) {
    long long y = 5;             /* sign extension: lo=5, hi=0 */
    return y;
}

unsigned long long zext_const(void) {
    unsigned long long y = 7;    /* zero extension: lo=7, hi=0 */
    return y;
}
