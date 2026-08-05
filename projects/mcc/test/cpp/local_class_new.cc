/* local_class_new.cc — m++: `new` on a class defined inside a function
 * body (local class).  Regression for a segfault where a local class's
 * constructor, parsed inline inside a function body, left the global
 * `curfunc` current-function pointer clobbered — so a later `new` in the
 * same enclosing function emitted into the (unfinished) ctor's IR block
 * and crashed.  Fixed by restoring curfunc in cpp_parse_method_body and
 * recording the class declaration scope (t->scope) for local classes.
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */

int
main(void)
{
    /* local class with a user constructor, `new T(...)` paren form */
    {
        struct Point { int x, y; Point(int a, int b) { x = a; y = b; } };
        Point *p = new Point(5, 6);
        if (p->x != 5 || p->y != 6) return 1;
        delete p;
    }

    /* local class with a user constructor, `new T{...}` braced form */
    {
        struct Pt { int x, y; Pt(int a, int b) { x = a; y = b; } };
        Pt *q = new Pt{7, 8};
        if (q->x != 7 || q->y != 8) return 2;
        delete q;
    }

    /* local aggregate class (no ctor), braced-init `new T{...}` */
    {
        struct R { int a, b, c; };
        R *r = new R{1, 2, 3};
        if (r->a != 1 || r->b != 2 || r->c != 3) return 3;
        delete r;
    }

    return 0;
}
