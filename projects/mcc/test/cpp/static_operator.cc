/* static_operator.cc — C++23 P1169: `static operator[]` / `static
 * operator()` (m++).
 *
 * A static operator has no implicit `this`; the object is an explicit
 * first parameter (`T&` / `T&&` / `T`).  Calls like `m[0, 1]` or
 * `f(10)` bind the object to that parameter.
 *
 * Covers:
 *  - static operator[] with an lvalue-reference object parameter,
 *    multidimensional subscript (C++23 P2128 multiple parameters)
 *  - single-argument static operator[]
 *  - static operator() functor call
 *  - a static method called through an object (`r.get()`)
 *  - const-qualified object parameter
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
struct Matrix {
    int data[4];
    static int &operator[](Matrix &m, int i, int j) {
        return m.data[i * 2 + j];
    }
};

struct Grid {
    int v[3];
    static int &operator[](Grid &g, int i) { return g.v[i]; }
};

struct Fun {
    int base;
    static int operator()(Fun &self, int x) { return self.base + x; }
};

struct RO {
    int v;
    static int read(const RO &r) { return r.v; }
};

int
main(void)
{
    /* multidimensional static operator[] */
    Matrix m;
    m[0, 0] = 1;
    m[0, 1] = 2;
    m[1, 0] = 3;
    m[1, 1] = 4;
    if (m.data[0] != 1 || m.data[1] != 2 ||
        m.data[2] != 3 || m.data[3] != 4) return 1;

    /* single-argument static operator[] */
    Grid g = {};   /* value-initialize so the untouched element is 0 */
    g[0] = 7;
    g[2] = 9;
    if (g.v[0] != 7 || g.v[1] != 0 || g.v[2] != 9) return 2;

    /* static operator() functor call */
    Fun f;
    f.base = 5;
    if (f(10) != 15) return 3;

    /* static method invoked through an object (const object) */
    const RO r = {42};
    if (r.read() != 42) return 4;

    return 0;
}
