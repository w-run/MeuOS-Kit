/* unnamed_param.cc — unnamed function/constructor parameters (defect M).
 *
 * Previously a constructor with an unnamed parameter (`B(int)`) SEGFAULTed
 * the compiler (cpp_parse_method_body bound parameter decls by name).
 * Unnamed parameters are accepted: the type participates in mangling /
 * overload resolution, the body simply cannot refer to them.
 *
 * Covers:
 *  - stack construction with an unnamed ctor parameter
 *  - scalar new with an unnamed ctor parameter
 *  - a free function with an unnamed parameter
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
class B {
public:
    B(int) { val = 3; }
    int val;
};

int twice(int) { return 42; }

int
main(void)
{
    B b(3);
    if (b.val != 3) return 1;

    B *p = new B(5);
    if (p->val != 3) return 2;
    delete p;

    if (twice(1) != 42) return 3;

    return 0;
}
