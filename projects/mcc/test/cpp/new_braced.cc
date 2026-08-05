/* new_braced.cc — C++11 `new T{args}` braced-init new expression (m++
 * end-to-end).
 *
 * Covers scalar braced-init (`new int{42}` value-initialises the heap
 * scalar), aggregate braced-init (`new Pt{3,4}` positionally assigns the
 * data members), braced-init through a user constructor
 * (`new Point{3,4}` overload-resolves the ctor), and scalar-array
 * braced-init (`new int[3]{1,2,3}` assigns each element, value-initializing
 * any element beyond the list -> 0).
 *
 * Array braced-init on a class element type (`new Pt[3]{...}`) is a known
 * TODO (m++ emits a clear "not implemented yet" diagnostic), so it is not
 * exercised here.
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */

struct Pt { int x, y; };       /* aggregate */
struct Point {
    int x, y;
    Point(int a, int b) : x(a), y(b) {}   /* user ctor */
};

int
main(void)
{
    /* scalar braced-init: value-initializes the heap scalar */
    int *p = new int{42};
    if (*p != 42) return 1;
    delete p;

    /* scalar braced-init with a different value */
    int *q = new int{100};
    if (*q != 100) return 2;
    delete q;

    /* aggregate braced-init: members assigned positionally through the
     * heap pointer */
    Pt *r = new Pt{3, 4};
    if (r->x != 3 || r->y != 4) return 3;
    delete r;

    /* braced-init through a user constructor */
    Point *s = new Point{5, 6};
    if (s->x != 5 || s->y != 6) return 4;
    delete s;

    /* scalar-array braced-init: every element assigned */
    int *a = new int[3]{1, 2, 3};
    if (a[0] != 1 || a[1] != 2 || a[2] != 3) return 5;
    delete[] a;

    /* scalar-array braced-init with a list shorter than n: the remaining
     * elements are value-initialized to 0 */
    int *b = new int[5]{1, 2, 3};
    if (b[0] != 1 || b[1] != 2 || b[2] != 3 || b[3] != 0 || b[4] != 0) return 6;
    delete[] b;

    /* empty scalar-array braced-init: everything value-initialized to 0 */
    int *c = new int[4]{};
    if (c[0] != 0 || c[1] != 0 || c[2] != 0 || c[3] != 0) return 7;
    delete[] c;

    return 0;
}
