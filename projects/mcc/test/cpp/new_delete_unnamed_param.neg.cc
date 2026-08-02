/* new_delete_unnamed_param.neg.cc — constructor with an UNNAMED parameter.
 *
 * m++ currently SEGFAULTS (rc=139) on any constructor whose parameter is
 * unnamed, e.g. `B(int) {}` — in any construction context (stack,
 * scalar new, array new).  Named parameters work fine; free functions
 * with unnamed parameters work fine.  See .issues/0802.md defect M.
 *
 * This test is a canary: it must NOT produce a working binary today
 * (compiler crashes = check-cpp-neg passes for the wrong reason).  When
 * defect M is fixed, this file must be revisited (either the ctor is
 * accepted and the test becomes positive, or it is cleanly rejected).
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
