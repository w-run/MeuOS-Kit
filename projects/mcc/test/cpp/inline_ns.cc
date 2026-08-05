/* C++11 inline namespace: its members are directly visible in the
 * enclosing (file) scope, as if `using namespace` were declared.
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
inline namespace V1 {
    int x = 42;
    int add(int a, int b) { return a + b; }
}

namespace V2 { int y = 7; }

int main(void) {
    if (x != 42) return 1;          /* inline namespace object visible */
    if (add(1, 2) != 3) return 2;   /* inline namespace function visible */
    if (V2::y != 7) return 3;       /* ordinary qualified access still works */
    if (V1::x != 42) return 4;      /* qualified access still works */
    return 0;
}
