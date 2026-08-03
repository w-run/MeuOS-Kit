/* variadic_pack_arity.neg.cc — expanding a pack into a target whose arity
 * does not match must be rejected.
 *
 * `one` takes exactly one parameter, so expanding a two-element pack into
 * `one(args...)` produces a two-argument call and must fail to typecheck.
 * The positive counterpart (empty and single-element expansions) lives in
 * variadic_empty_pack.cc; this guards the failing side of the same
 * expansion path.
 *
 * check-cpp-neg compiles this expecting failure.
 */
template <typename T> int one(T a) { return a; }

template <typename... Args> int fwd(Args... args) { return one(args...); }

int
main(void)
{
    return fwd(1, 2);   /* one(1, 2): too many arguments */
}
