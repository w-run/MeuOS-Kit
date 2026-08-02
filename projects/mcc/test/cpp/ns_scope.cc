/* ns_scope.cc — namespace scoping (C.2.3, m++ end-to-end).
 *
 * Covers:
 *  - single-level namespace: classes, inheritance inside the namespace,
 *    free functions taking namespace class types, namespace variables
 *  - qualified access to a namespace member (`Geo::Point`)
 *  - nested namespaces: variables and functions accessed via a multi-level
 *    qualified path (`Outer::Inner::depth`)
 *  - name shadowing: a namespace-internal name wins over a global name
 *
 * NOTE: classes declared inside a NESTED namespace are currently not
 * reachable via `Outer::Inner::Class` in m++ (see
 * test/cpp/pending/nested_ns_class.cc); this test keeps classes in
 * single-level namespaces and uses multi-level paths only for
 * variables/functions.
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */

int gversion = 1;                /* global; distinct from Geo::version */
int shadowed = 99;               /* global value */

namespace Geo {
    int version = 2;             /* namespace variable (no global collision) */

    class Point {
    public:
        int x, y;
        Point() { x = 0; y = 0; }
        void set(int a, int b) { x = a; y = b; }
        int sum() { return x + y; }
    };
    class Point3D : public Point {
    public:
        int z;
        Point3D() { z = 0; }
        void setz(int v) { z = v; }
        int depth() { return z; }
    };

    int origin = 0;
    int dist(Point p) { return p.x * 2; }
}

namespace Outer {
    namespace Inner {
        int depth = 3;
        int level() { return depth * 10; }
    }
    int top = 5;
}

int main(void) {
    /* single-level namespace classes + qualified access */
    Geo::Point p;
    p.set(3, 4);
    if (p.sum() != 7) return 1;

    /* inheritance inside a namespace */
    Geo::Point3D q;
    q.set(1, 2);
    q.setz(9);
    if (q.sum() != 3) return 2;
    if (q.depth() != 9) return 3;

    /* namespace variables + function taking a namespace class */
    if (Geo::origin != 0) return 4;
    if (Geo::dist(p) != 6) return 5;

    /* namespace variables are independent from globals */
    if (gversion != 1) return 6;     /* global */
    if (Geo::version != 2) return 7; /* Geo::version */

    /* namespaces stay independent of globals */
    if (gversion != 1) return 8;

    /* nested namespaces: variables and functions via qualified path */
    if (Outer::Inner::depth != 3) return 9;
    if (Outer::Inner::level() != 30) return 10;
    if (Outer::top != 5) return 11;

    /* global still reachable when not shadowed */
    if (shadowed != 99) return 12;

    return 0;
}
