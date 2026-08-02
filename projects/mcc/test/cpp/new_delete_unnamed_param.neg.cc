/* new_delete_unnamed_param.neg.cc — regression guard for defect M
 * (unnamed-parameter constructor SEGV).
 *
 * defect M: m++ SEGFAULTED (rc=139) on any constructor with an unnamed
 * parameter, e.g. `B(int) {}`, in any construction context.  Fixed by
 * 4d93a66 (parameter binding skips NULL names); `B b(3)` / `new B(5)`
 * now work and named parameters are unaffected.
 *
 * This file is the minimal reproduction of that crash path.  After the
 * fix it is a clean compile-time rejection: a class whose only
 * constructor takes arguments (B(int), with no default ctor) cannot be
 * array-allocated, so `new B[2]` fails with "no matching constructor
 * for 'new B[]'".  check-cpp-neg requires a non-zero compile exit;
 * should the SEGV ever regress, the exit is a crash instead of the
 * intended clean rejection and this guard flags it.
 */
class B {
public:
    B(int) { }
    int val;
};

int main(void) {
    B *b = new B[2];
    delete[] b;
    return 0;
}
