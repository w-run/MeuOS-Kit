/* i386 i64-compare truth-value gate (defect #16).
 *
 * The i64 equality code path in i386_memit.c::emit_setccr ended its equal
 * branch with `sete %al` but no zero-extension, so the %eax high 24 bits
 * were stale from the preceding `movl a.lo,%eax`.  A `f()==const` comparison
 * then yielded a garbage 32-bit "truth" value (e.g. 0x5566...00 instead of
 * 0/1), which corrupted downstream boolean logic (`?:`, `&&`, phi picks).
 *
 * This test asserts at the asm level that the EQ branch zero-extends with
 * movzbl (matching the NE/other branches) so the result is a clean 0/1.
 */
long long f(void) { return 0x55667788LL; }
int main(void) {
    long long r = f();
    /* force a runtime i64 comparison (defeat folding) */
    return (r == 0x55667788LL) ? 42 : 1;
}
