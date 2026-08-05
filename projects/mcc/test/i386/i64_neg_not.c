/* i386 i64 NEG/NOT half-store register-name gate (defect class: emit
 * layer operand formatting, found during the i386/x86_64 consistency sweep).
 *
 * i386_memit.c's i64 NEG/NOT stored the lo/hi halves via
 * scratch_to_dst_i64_lo/hi with a bare register name ("edx"/"ecx"/"eax"),
 * which i64_store_half printed verbatim -> `movl edx, off(%ebp)` (no `%`).
 * The GNU assembler silently accepted it as a zero operand (warning:
 * "missing operand; zero assumed") and produced garbage, so i64 negation
 * and bitwise-NOT returned wrong values.
 *
 * All operands here are LOCAL (not function params) so the gate exercises
 * the NEG/NOT emit path without depending on the separate i64 stack-param
 * slot assignment.  The compile gate asserts no bare (un-percented) GPR
 * appears as a movl source/dest.
 */
long long lneg(long long a) { return -a; }
long long lnot(long long a) { return ~a; }
long long cneg(void) { return -0x1122334455667788LL; }
long long cnot(void) { return ~0x123456789ABCDEF0LL; }

int main(void) {
    if (lneg(5LL) != -5LL) return 1;
    if (lneg(-5LL) != 5LL) return 2;       /* local -5, not a param */
    if (lnot(0LL) != -1LL) return 3;
    if (lnot(0x123456789ABCDEF0LL) != ~0x123456789ABCDEF0LL) return 4;
    if (cneg() != -0x1122334455667788LL) return 5;
    if (cnot() != ~0x123456789ABCDEF0LL) return 6;
    return 0;
}
