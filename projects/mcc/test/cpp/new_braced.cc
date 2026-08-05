/* new_braced.cc — C++11 `new T{args}` braced-init new expression (m++
 * end-to-end).
 *
 * Covers scalar braced-init (`new int{42}` value-initialises the heap
 * scalar), aggregate braced-init (`new Pt{3,4}` positionally assigns the
 * data members), and braced-init through a user constructor
 * (`new Point{3,4}` overload-resolves the ctor).  All of these lower to
 * malloc + value-init/aggregate/ctor, returning the pointer.
 *
 * Array braced-init `new int[n]{...}` is a known TODO (m++ emits a clear
 * "not implemented yet" diagnostic), so it is not exercised here.
 *
 * Note: the class types are declared at file scope — m++ `new` on a
 * struct defined inside a function body currently segfaults (a separate
 * pre-existing defect, unrelated to braced-init).
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

    return 0;
}
