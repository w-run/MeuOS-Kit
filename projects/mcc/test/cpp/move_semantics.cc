/* move_semantics.cc — C++11 move semantics / rvalue references (m++).
 *
 * Covers:
 *  - `T &&` rvalue-reference parameters and members
 *  - move vs copy constructor overloading (lvalue → copy, rvalue → move)
 *  - free-function overloads distinguished by value category
 *    (`take(Vec&)` vs `take(Vec&&)` vs `take(Vec)`)
 *  - member-function overloads distinguished by value category
 *  - binding a temporary to an rvalue reference (`Vec &&rv = Vec(8)`)
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
extern int printf(const char *, ...);

class Vec {
public:
    int m;
    Vec() { m = 0; }
    Vec(int v) { m = v; }
    Vec(Vec &other) { m = other.m + 1000; }   /* copy ctor */
    Vec(Vec &&other) { m = other.m + 2000; }  /* move ctor */
    int get() { return m; }
};

/* free-function overloads: lvalue ref, rvalue ref, by value */
int take(Vec &v) { return v.m + 100; }
int take(Vec &&v) { return v.m + 200; }
int takev(Vec v) { return v.m; }

class Handler {
public:
    int m;
    Handler() { m = 0; }
    void set(Vec &v) { m = v.m + 100; }   /* lvalue ref method */
    void set(Vec &&v) { m = v.m + 200; }  /* rvalue ref method */
};

int main(void) {
    /* move ctor is picked for an rvalue, copy ctor for an lvalue */
    Vec a(5);
    Vec b(a);               /* lvalue → copy ctor */
    if (a.m != 5) return 1;
    if (b.get() != 1005) return 2;

    /* free-function overload by value category */
    if (take(a) != 105) return 3;        /* lvalue → lvalue ref */
    if (take(Vec(7)) != 207) return 4;   /* rvalue temp → rvalue ref */
    if (takev(a) != 5) return 5;         /* by-value works */

    /* member-function overload by value category */
    Handler h;
    h.set(a);              /* lvalue → lvalue ref */
    if (h.m != 105) return 6;
    h.set(Vec(8));         /* rvalue temp → rvalue ref */
    if (h.m != 208) return 7;

    /* bind a temporary to an rvalue reference */
    Vec &&rv = Vec(9);
    if (rv.m != 9) return 8;

    printf("move_semantics: passed\n");
    return 0;
}
