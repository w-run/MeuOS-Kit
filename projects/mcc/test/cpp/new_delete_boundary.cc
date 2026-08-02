/* new_delete_boundary.cc — new/delete edge cases (m++ end-to-end).
 *
 * Covers:
 *  - `new T(args)` used directly as a function argument (no temp variable)
 *  - `new` used as a return value (a factory returning `new T(...)`)
 *  - `delete` of the value returned from a function
 *  - `new int` result used in an expression
 *
 * `new T[n]` array allocation and placement new are rejected by m++ and
 * covered by the matching .neg.cc tests instead.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
extern int printf(const char *, ...);

class Counter {
public:
    Counter()       { val = 0; }
    Counter(int v)  { val = v; }
    int val;
};

static int take(Counter *p) { return p->val; }

static Counter *make(int v) { return new Counter(v); }

int main(void) {
    /* new result passed straight to a function */
    if (take(new Counter(7)) != 7) return 1;

    /* new as a return value */
    Counter *q = make(21);
    if (q->val != 21) return 2;
    delete q;

    /* delete of a function-returned object */
    Counter *r = make(3);
    if (r->val != 3) return 3;
    delete r;

    /* new of a builtin scalar used inline in an expression */
    int *p = new int;
    *p = 10;
    if ((*p + 5) != 15) return 4;
    delete p;

    /* factory chaining: new result passed on without storing */
    if (take(make(9)) != 9) return 5;

    printf("new_delete_boundary: passed\n");
    return 0;
}
