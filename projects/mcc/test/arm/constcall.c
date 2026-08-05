/* arm call-with-constant-arguments compile gate.
 *
 * A call whose arguments are literal constants (`add(20, 22)`) must have
 * each constant materialized as an immediate move into the argument
 * registers r0/r1, not as an 8-byte slot move.  The pre-fix arm
 * `case MMOP_MOV` treated an i32 destination with an i64-typed constant
 * source (the frontend types all pool constants i64) as an i64 slot move,
 * so the `bl add` was emitted with r0/r1 never loaded and the call read
 * garbage.  Runtime-matrix rr_call (return 42) surfaced this on arm.
 *
 * Compile-level: assert the calling function sets up r0/r1 from immediates
 * and never falls into the i64 slot-load path for the constants.
 */
static int add(int a, int b) { return a + b; }

int main(void) {
    return add(20, 22);   /* 0x14, 0x16 -> r0/r1 */
}
