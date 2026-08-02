/* C++ free-function overloading at file scope.
 *
 * A same-name file-scope function with a different parameter signature
 * (count or type) is an overload, not a conflicting redeclaration.  The
 * first-declared overload keeps the plain symbol; later overloads are
 * registered under the parameter-encoded mangled name (`helper_ii`),
 * and each call site resolves the matching overload from the argument
 * types.
 *
 * - arity overloads: helper(int) vs helper(int, int)
 * - type overloads:  helper(int) vs helper(double)
 * - reference overloads: f(Vec) vs f(Vec&), lvalue prefers the ref
 */
struct Vec { int x, y; };

int helper(int a) { return a + 1; }
int helper(int a, int b) { return a + b; }
int helper(double a) { return (int)(a * 2); }

int f(Vec v) { return v.x + v.y; }        /* by value  -> f_oVec */
int f(Vec &v) { return v.x * v.y; }       /* by ref    -> f_RoVec */

int main(void) {
    if (helper(5) != 6) return 1;         /* helper(int) */
    if (helper(5, 3) != 8) return 2;      /* helper(int, int) */
    if (helper(3.0) != 6) return 3;       /* helper(double) */
    Vec a = {3, 4};
    if (f(a) != 12) return 4;             /* lvalue binds the ref overload */
    Vec b = {2, 5};
    if (f(b) != 10) return 5;             /* lvalue binds the ref overload */
    return 0;
}
