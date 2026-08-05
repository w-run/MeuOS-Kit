/* i386 i64 stack-param reception gate (compile-time).
 *
 * On i386, a `long long` parameter arrives on the caller-pushed stack at
 * [ebp+8], [ebp+12], [ebp+16]...  mabi_selpar must load the two 32-bit
 * halves into the parameter value's real frame slots.  Before the fix, it
 * hard-wired the LOAD destinations to `dst->slot` which is -1 until the
 * regalloc runs, producing `movl -1(%ebp)/3(%ebp)/-5(%ebp)` garbage reads
 * (x86-i64param).  The fix emits a single MMOP_LOAD (MT_I64) into dst so
 * the emitter writes both halves into dst's phislot-forced frame slot.
 *
 * This gate compiles the function and asserts (a) the incoming halves are
 * read from the real argument offsets [ebp+8]/[ebp+12], and (b) no bogus
 * `1(%ebp)`/`3(%ebp)`/`-1(%ebp)` slot reads appear in the reception path.
 */
long long passthru(long long a, long long b) { return a + b; }
