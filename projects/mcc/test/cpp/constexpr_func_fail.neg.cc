/* constexpr_func_fail.neg.cc — a constexpr function folded in a
 * static_assert that evaluates to false must be rejected.
 */
constexpr int sq(int x) { return x * x; }

static_assert(sq(4) == 15, "deliberately wrong");

int main(void) {
    return 0;
}
