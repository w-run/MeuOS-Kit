/* ctad.cc — C++17 class template argument deduction (CTAD, m++).
 *
 * `Vec v(a, b)` — a class template used without explicit `<...>`
 * arguments deduces its template parameters from the constructor-call
 * argument types.
 *
 * Covers:
 *  - CTAD for a single-parameter class template (`Vec v(arr, 3)` → Vec<int>)
 *  - member access on the deduced object (`v.n`)
 *  - CTAD in function bodies and at file scope
 *  - explicit template arguments still work alongside CTAD
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
template <typename T>
class Vec {
public:
    T *data;
    int n;
    Vec(T *d, int sz) { data = d; n = sz; }
    T sum(void) {
        T r = 0;
        for (int i = 0; i < n; ++i)
            r += data[i];
        return r;
    }
};

int garr[3] = {10, 20, 30};
Vec gg(garr, 3);   /* file-scope CTAD → Vec<int> */

int
main(void)
{
    int arr[3] = {1, 2, 3};
    Vec v(arr, 3);                /* CTAD: Vec<int> */
    if (v.n != 3) return 1;
    if (v.sum() != 6) return 2;

    double darr[2] = {1.5, 2.5};
    Vec w(darr, 2);               /* CTAD: Vec<double> */
    if (w.n != 2) return 3;
    if (w.sum() != 4.0) return 4;

    /* explicit template arguments still work */
    Vec<int> e(arr, 3);
    if (e.sum() != 6) return 5;

    /* file-scope CTAD object */
    if (gg.sum() != 60) return 6;

    return 0;
}
