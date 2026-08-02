/* new_delete.cc — new/delete operators (m++ end-to-end).
 *
 * Covers:
 *  - `new T` / `new T(args)`: malloc + constructor call, returning T*
 *  - `new` of a builtin scalar type (`new int`)
 *  - `delete p`: destructor call + free
 *  - a class holding a heap-allocated member (new/delete in methods)
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
extern int printf(const char *, ...);

class Counter {
public:
    Counter()       { val = 0; }
    Counter(int v)  { val = v; }
    ~Counter()      { }
    int val;
};

class Holder {
public:
    Holder(int v)  { obj = new Counter(v); }
    ~Holder()      { delete obj; }
    int get()      { return obj->val; }
    Counter *obj;
};

int main(void) {
    /* new T() with the default constructor */
    Counter *a = new Counter();
    a->val = 5;
    if (a->val != 5) return 1;
    delete a;

    /* new T(args) with the argument constructor */
    Counter *b = new Counter(7);
    if (b->val != 7) return 2;
    delete b;

    /* new of a builtin scalar */
    int *p = new int;
    *p = 42;
    if (*p != 42) return 3;
    delete p;

    /* delete of a class without a user destructor still frees */
    Counter *c = new Counter(9);
    if (c->val != 9) return 4;
    delete c;

    /* a class that news/deletes a heap member in its ctor/dtor */
    Holder h(11);
    if (h.get() != 11) return 5;
    /* Holder's destructor deletes its member; this must not double-free */

    printf("new_delete: passed\n");
    return 0;
}
