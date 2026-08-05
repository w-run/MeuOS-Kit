/* i386 i64-shift compile gate.
 *
 * A 64-bit shift on i386 (value split across two 32-bit slot halves) must
 * (1) take the variable shift count in %cl (8-bit) — never %ecx — and
 * (2) use the plain shl/shr/sar mnemonics (mt/as rejects the GNU shll
 * aliases).  The pre-fix emitter generated `shll %ecx, %eax`, which is not
 * a valid encoding (shift counts are %cl) and failed the mt/as grammar.
 * It also shifted each half independently with no cross-half carry, so
 * `1LL<<40` dropped the bit.  Runtime-matrix rr_i64 (expect 42) caught it.
 */
long long shl40(long long v) { return v << 40; }
long long shr33(long long v) { return v >> 33; }
long long asr7(long long v) { return v >> 7; }

int main(void) {
    long long x = (long long)1 << 40;   /* 0x10000000000 */
    return (x >> 32) == 256 ? 42 : 0;
}
