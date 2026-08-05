/* arm runtime-matrix style i64-shift compile gate.
 *
 * A correct 64-bit shift on arm (value split across two 32-bit halves)
 * must move bits ACROSS the halves rather than shifting each half
 * independently.  `1LL << 40` puts the sole set bit into the HIGH half
 * (bit 40 = 8 + 32), so the emitter must emit the s>=32 adjustment
 * `sub r12, r12, #32` before the shift that feeds the high half.  The
 * pre-fix per-half fallback emitted a bare `lsl` on each half with no
 * shift-amount adjustment and no cross-half contribution, so `1<<40`
 * cleared the low half to 0 and left the high half 0 (returns 0 instead
 * of the expected 256 in the high half).
 *
 * Compile-level: the runtime matrix (rr_i64) assembles, links and runs
 * the binary when the arm sysroot + qemu-arm are present; this gate runs
 * even when they are not, by asserting the generated assembly.
 */
long long shl40(long long v) { return v << 40; }
long long shr33(long long v) { return v >> 33; }
long long asr7(long long v) { return v >> 7; } /* arithmetic, < 32 carry */

int main(void) {
    long long x = (long long)1 << 40;   /* 0x10000000000 */
    return (x >> 32) == 256 ? 42 : 0;
}
