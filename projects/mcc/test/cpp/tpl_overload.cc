/* tpl_overload.cc — template / overload interaction cases (m++ end-to-end).
 *
 * Covers interactions between C.2.8 function templates and C.2.3 overload
 * support:
 *  - a non-template function and a same-named template coexist; the
 *    non-template shadows the template for ordinary calls (m++ current
 *    semantics: even double arguments implicitly convert to int and bind
 *    the non-template)
 *  - a template body that triggers a user operator overload (Vec + Vec)
 *  - a template body that calls a non-template free function
 *  - a template body that mutates member state of its type argument
 *  - a class-template instantiation passed to a function template
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */

/* --- non-template shadows same-named template --- */
int pick(int a, int b) { return a - b; }
template <typename T> T pick(T a, T b) { return a > b ? a : b; }

/* --- template body triggers operator overload --- */
class Vec {
public:
    Vec(int v) { m = v; }
    int m;
};
Vec operator+(Vec a, Vec b) { Vec r(a.m + b.m); return r; }
template <typename T> T sum3(T a, T b, T c) { return (a + b) + c; }

/* --- template body calls a non-template free function --- */
int helper(int a) { return a + 1; }
template <typename T> int t_helper(T v) { return helper(v); }

/* --- template body mutates member state of its type argument --- */
class Counter {
public:
    int count;
    Counter() { count = 0; }
    void inc() { count = count + 1; }
    int get() { return count; }
};
template <typename T> int bump(T obj, int n) {
    for (int i = 0; i < n; i = i + 1) obj.inc();
    return obj.get();
}

/* --- class-template instantiation as a function-template argument --- */
template <typename T> class Box {
public:
    T val;
    Box(T v) { val = v; }
    T get() { return val; }
};
template <typename T> double boxval(T obj) { return (double)obj.get(); }

int main(void) {
    /* non-template wins on an exact int match */
    if (pick(5, 3) != 2) return 1;

    /* m++ current semantics: the non-template shadows the template even
     * for double arguments (implicit conversion to int).  Locks in the
     * implemented resolution order; revisit if overload ranking becomes
     * standard-conforming. */
    if (pick(5.0, 3.0) != 2) return 2;

    /* template body triggering a user operator overload */
    Vec v1(1), v2(2), v3(3);
    Vec vs = sum3(v1, v2, v3);
    if (vs.m != 6) return 3;
    if (sum3(1, 2, 3) != 6) return 4;

    /* template body calling a non-template free function */
    if (t_helper(5) != 6) return 5;

    /* template body mutating member state */
    Counter c;
    if (bump(c, 3) != 3) return 6;

    /* class-template instantiation passed to a function template */
    Box<int> bx(42);
    if (boxval(bx) != 42) return 7;
    Box<double> bd(2.5);
    if (boxval(bd) != 2.5) return 8;

    return 0;
}
