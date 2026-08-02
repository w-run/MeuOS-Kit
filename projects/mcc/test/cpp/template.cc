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

int
main(void)
{
    if (max(3, 7) != 7) return 1;            /* int instantiation */
    if (max(1.5, 2.5) != 2.5) return 2;      /* double instantiation */
    if (max(10, 20) != 20) return 3;         /* cache reuse */
    if (add(1, 2.5) != 3.5) return 4;        /* T=int, U=double */

    Counter c;
    if (probe(c, 3) != 3) return 5;          /* class-type instantiation */
    Counter c2;
    if (probe(c2, 5) != 5) return 6;

    if (square(max(3, 4)) != 16) return 7;   /* nested template calls */

    Holder h;
    if (h.apply() != 9) return 8;

    return 0;
}
