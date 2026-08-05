/* Negative test: `new T[n]` on a class whose only constructor takes
 * arguments must be rejected — m++ always default-constructs array
 * elements (there is no `new (ptr) T`-style per-element argument list),
 * so a class without a matching default ctor is a compile error.
 */
class CtorOnly {
public:
    CtorOnly(int v) { val = v; }
    int val;
};

int
main(void)
{
    CtorOnly *a = new CtorOnly[3];
    delete[] a;
    return 0;
}
