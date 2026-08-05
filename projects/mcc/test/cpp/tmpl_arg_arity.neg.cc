/* tmpl_arg_arity.neg.cc — supplying more template arguments than the
 * class template declares must be rejected.
 *
 * `Wrap` has a single type parameter, so `Wrap<int, int>` is ill-formed:
 * the extra argument has nowhere to bind.  Distinct from
 * arg_type_mismatch.neg.cc (function *call* arguments) and
 * too_many_args.neg.cc (member function arity) — this one guards the
 * template argument list itself.
 *
 * check-cpp-neg compiles this expecting failure.
 */
template <typename T> struct Wrap { T v; };

int
main(void)
{
    Wrap<int, int> w;   /* too many template arguments for 'Wrap' */
    w.v = 1;
    return 0;
}
