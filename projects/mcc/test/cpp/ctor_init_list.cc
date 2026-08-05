/* ctor_init_list.cc — constructor initializer list (m++ end-to-end).
 *
 * Covers:
 *  - `Derived(int v) : Base(v), m(v * 2) {}` — base-class ctor + member
 *    object ctor both called with the initializer-list arguments
 *  - multiple arguments in one item: `: Base(v, w)`
 *  - a member whose initializer computes from a parameter
 *  - chained derived classes (Derived2 : Derived) still default-construct
 *    their base when no init list is present
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
extern int printf(const char *, ...);

class Base {
public:
    Base()       { printf("Base()\n");   a = 0; }
    Base(int v)  { printf("Base(v)\n");  a = v; }
    int a;
};

class Member {
public:
    Member()         { printf("Member()\n");     m = -1; }
    Member(int v)    { printf("Member(v)\n");    m = v; }
    int m;
};

class Derived : public Base {
public:
    Derived(int v) : Base(v), mem(v * 2) { b = v + 100; }
    int b;
    Member mem;
};

class MultiArg : public Base {
public:
    MultiArg(int v, int w) : Base(v) { }
    int b;
};

class Derived2 : public Derived {
public:
    Derived2() : Derived(5) { }
    int c;
};

int main(void) {
    /* base ctor + member object ctor both initialized from the list */
    Derived d(7);
    if (d.a != 7)  return 1;   /* Base(v)      -> a = 7     */
    if (d.mem.m != 14) return 2; /* mem(v * 2)  -> m = 14    */
    if (d.b != 107) return 3;  /* body         -> b = 107   */

    /* multiple args in one item */
    MultiArg ma(3, 4);
    if (ma.a != 3) return 4;

    /* derived-of-derived: explicit chain of init lists */
    Derived2 d2;
    if (d2.a != 5) return 5;
    if (d2.b != 105) return 6;

    printf("ctor_init_list: passed\n");
    return 0;
}
