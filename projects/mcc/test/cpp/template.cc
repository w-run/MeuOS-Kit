/* template.cc — C.2.8 function templates (m++ end-to-end).
 *
 * Covers instantiate-on-first-use function templates: basic `T max(T,T)`
 * with int/double instantiations, a multi-parameter template with mixed
 * argument types, class-type instantiations with member-function calls in
 * the body, nested template calls (template bodies calling templates), and
 * template calls from inside class member functions.
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */
template <typename T> T max(T a, T b) { return a > b ? a : b; }
template <typename T, typename U> U add(T a, U b) { return a + b; }
template <typename T> T square(T v) { return v * v; }
/* function-template explicit specialization (`template <>`): the int
 * specialization of `xsp` returns the loser+1 (distinct from the generic
 * which returns `a>b?a:b`), so a matching call proves the specialization
 * shadows the primary body. */
template <typename T> T xsp(T a, T b) { return a > b ? a : b; }
template <> int xsp<int>(int a, int b) { return a > b ? b + 1 : a + 1; }

class Counter {
public:
    int count;
    Counter() { count = 0; }
    void inc() { count = count + 1; }
    int get() { return count; }
};

/* template body that calls a member function of its type argument */
template <typename T> int probe(T obj, int n) {
    for (int i = 0; i < n; i = i + 1) obj.inc();
    return obj.get();
}

class Holder {
public:
    int apply() {
        /* template call from inside a member function */
        return max(5, 9);
    }
};

template <typename T> class Box {
public:
    T val;
    Box() { val = 0; }
    Box(T v) { val = v; }
    T get() { return val; }
};

template <typename T> class Pair {
public:
    T a, b;
    Pair(T x, T y) { a = x; b = y; }
    T sum() { return a + b; }
};

/* --- C.2.8 member templates (template methods in a class) --- */

class Wrapper {
public:
    int base;
    Wrapper() { base = 5; }
    template <typename T> T get() { return (T)base; }
    template <typename T> T add(T v) { return (T)base + v; }
};

class MBox {
public:
    int val;
    MBox() { val = 0; }
    MBox(int v) { val = v; }
    template <typename T> T get() { return (T)val; }
    template <typename T> void set(T v) { val = (int)v; }
    int twice() { return val * 2; }
};

/* class template whose methods are member templates */
template <typename U> class TBox {
public:
    U val;
    TBox(U v) { val = v; }
    template <typename T> T conv() { return (T)val; }
};

int
main(void)
{
    if (max(3, 7) != 7) return 1;            /* int instantiation */
    if (max(1.5, 2.5) != 2.5) return 2;      /* double instantiation */
    if (max(10, 20) != 20) return 3;         /* cache reuse */
    if (add(1, 2.5) != 3.5) return 4;        /* T=int, U=double */

    /* template explicit specialization: xsp<int> shadows the primary body.
     * spec: a>b ? b+1 : a+1  (primary would return the loser unchanged) */
    if (xsp(3, 7) != 4) return 41;           /* a+1 (3+1), not generic 7 */
    if (xsp(5, 2) != 3) return 42;           /* b+1 (2+1), not generic 5 */

    Counter c;
    if (probe(c, 3) != 3) return 5;          /* class-type instantiation */
    Counter c2;
    if (probe(c2, 5) != 5) return 6;

    if (square(max(3, 4)) != 16) return 7;   /* nested template calls */

    Holder h;
    if (h.apply() != 9) return 8;

    /* class templates */
    Box<int> bx(42);
    if (bx.get() != 42) return 9;         /* int instantiation */
    Box<double> bd(2.5);
    if (bd.get() != 2.5) return 10;       /* double instantiation */
    Box<int> bx2;
    if (bx2.get() != 0) return 11;        /* cache reuse */
    Pair<int> pr(10, 4);
    if (pr.sum() != 14) return 12;
    Pair<int> q(max(3, 8), max(1, 2));  /* class + function templates */
    if (q.sum() != 10) return 13;

    /* member templates */
    Wrapper wp;
    if (wp.get<int>() != 5) return 14;        /* explicit <int> */
    if (wp.get<double>() != 5.0) return 15;   /* explicit <double> */
    if (wp.add<int>(2) != 7) return 16;       /* explicit + arg */
    if (wp.add<double>(0.5) != 5.5) return 17;

    MBox m0(42);
    if (m0.get<int>() != 42) return 18;
    if (m0.get<double>() != 42.0) return 19;
    MBox m1;
    m1.set<double>(3.75);                     /* explicit <double>, arg */
    if (m1.get<int>() != 3) return 20;
    MBox m2(100);
    if (m2.get<int>() != 100) return 21;      /* cache reuse */
    if (m2.twice() != 200) return 22;         /* normal call after templates */

    /* member template of a class template instantiation */
    TBox<int> tb(42);
    if (tb.conv<double>() != 42.0) return 23;
    if (tb.conv<int>() != 42) return 24;

    return 0;
}
