/* constexpr_div_zero.neg.cc — a constexpr call that divides by zero is not
 * a constant expression and must be rejected in a static_assert.
 *
 * Division by zero has no defined value, so `div0(4)` cannot be folded;
 * the static_assert operand is therefore not an integer constant
 * expression.  Distinct from constexpr_func_fail.neg.cc (which folds
 * successfully to a *false* assertion) — here the fold itself must fail.
 *
 * check-cpp-neg compiles this expecting failure.
 */
constexpr int div0(int x) { return x / 0; }

static_assert(div0(4) == 0, "div0 is not a constant expression");

int
main(void)
{
    return 0;
}
