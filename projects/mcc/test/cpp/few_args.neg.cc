/* Negative test: calling a function with too few arguments (declared
 * with one int parameter, called with none) is ill-formed. Distinct from
 * too_many_args.neg.cc (member function arity) and arg_type_mismatch
 * (argument type) — this pins free-function arity at the call site.
 * check-cpp-neg expects compilation failure.
 */
int f(int);
int main(void) {
    return f();           /* too few arguments to 'f' */
}
