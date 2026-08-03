/* Negative test: supplying a non-type (int) where a class template
 * expects a type parameter is ill-formed. Distinct from
 * tmpl_arg_arity.neg.cc (too many args) — this pins the *kind* of the
 * template argument. check-cpp-neg expects compilation failure.
 */
template <typename T> struct S { T v; };
int main(void) {
    S<1> s;               /* '1' is not a type */
    return 0;
}
