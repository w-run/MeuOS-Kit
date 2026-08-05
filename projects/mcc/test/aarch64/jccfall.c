/* aarch64 JCC fallthrough gate (defect: i64 param matrix hang, exit=124).
 *
 * aarch64_memit.c emitted JCC terminators as a single conditional branch
 * (cbnz/cbz/b.cc) to the s1 ("taken") block and relied on physical block
 * order for the s2 (fallthrough) successor.  When the emitter lays blocks
 * out in fm->link order, the fallthrough target is not always adjacent, so
 * a JCC whose condition was false fell through into an arbitrary block and
 * infinite-looped (the rr_i64param matrix program's multi-check main looped
 * back into its entry dispatch block).  The fix emits an explicit
 * `b .L<fn>.bb<s2>` after every JCC (mirroring x86_64).
 *
 * This source uses multiple sequential conditional checks so the emitter
 * produces several JCCs; the gate asserts each is followed by an explicit
 * fallthrough branch.
 */
long long addll(long long a, long long b) { return a + b; }

int main(void) {
    if (addll(20, 22) != 42) return 1;
    if (addll(1, 2) != 3) return 2;
    return 42;
}
