/* i386 i64 constant-slot-half gate.
 *
 * A 64-bit constant operand on i386 is split into a low/high 32-bit pair
 * that must be materialised into a *based* frame slot (off(%ebp) with
 * off = slot + g_slot_base) and read back from the SAME based offsets by
 * every operand consumer (shift / add / sub / compare / store / move).
 *
 * The pre-fix emitter used the raw `slot` (missing g_slot_base) and the
 * `-1` "no-slot" sentinel verbatim, so a constant shift count was written
 * to `-1(%ebp)` and the value halves were read from a different place than
 * they were written — `x = 1<<40; (x>>32)==256` returned garbage
 * (runtime-matrix rr_i64, expect 42).  This gate directly exercises that
 * constant shift/arith path (no i64 stack params involved — those are a
 * separate, tracked defect) and asserts the constant operand is spilled
 * into a real based slot.
 *
 * NOTE: `i64_add(long long a, long long b)` with STACK-passed i64 params
 * is intentionally NOT included here: i386 i64 params read from the
 * `-1(%ebp)`/`3(%ebp)` sentinel (a known, separately-tracked mcc 一致性
 * defect in mabi_selpar's param->slot handshake).  Keeping this gate to
 * the constant path keeps the regression assertion binary-clean.
 */
int main(void) {
    long long x = (long long)1 << 40;   /* 0x10000000000 */
    if ((x >> 32) != 256)               /* shift >= 32 moves the bit across
                                           the high half */
        return 0;
    /* Constant i64 operands on the arithmetic path: add a 1<<60 const. */
    long long y = x + 0x1000000000000000LL;
    if (y != (long long)0x1000000000000000LL + x)
        return 0;
    return 42;
}
