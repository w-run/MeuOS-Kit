/* runtime-matrix: i64 stack params/returns on 32-bit targets. expect exit 42.
 *
 * A 64-bit integer parameter passed on the stack (i386 cdecl / AAPCS-ish
 * 32-bit ABI) must be received into the callee's frame slot and the caller
 * must push both halves.  Regression for x86-i64param: before the fix i386
 * read bogus -1/3(%ebp) slots for the param's halves and returned garbage.
 *
 * NOTE: keep operands small so every backend (aarch64/loongarch64 included)
 * can materialise the i64 constants; the i64 neg / int-truncation / big-const
 * paths that hang aarch64 or overflow the loongarch64 li.d immediate are
 * covered by separate defects, not this matrix program.
 */
long long addll(long long a, long long b) { return a + b; }

int main(void) {
    if (addll(20, 22) != 42) return 1;   /* i64 params + const args + return */
    if (addll(1, 2) != 3) return 2;
    if (addll(1000000LL, 999999LL) != 1999999LL) return 3;
    return 42;
}
