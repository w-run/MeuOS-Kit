/* ctor_init_list_boundary.cc — constructor initializer-list edge cases.
 *
 * Covers:
 *  - a three-level inheritance chain where every level passes an
 *    argument up through its init list (`C(x) : B(x+1) : A(x+1)`)
 *  - an expression as an initializer value (`mem(f(x))` — a function
 *    call in the init list)
 *  - a single init list mixing a base-class item and a member-object
 *    item with different expressions
 *
 * `int &ref` reference members are not supported by m++ yet — that is
 * covered by ref_member_init.neg.cc.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
extern int printf(const char *, ...);

class A {
public:
    A(int x) { a = x; }
    int a;
};

class B : public A {
public:
    B(int x) : A(x + 1) { b = x * 2; }
    int b;
};

class C : public B {
public:
    C(int x) : B(x + 1) { c = x * 3; }
    int c;
};

static int bump(int x) { return x + 10; }

class M {
public:
    M(int x) { m = x; }
    int m;
};

/* one init list mixing base + member with different expressions */
class D : public C {
public:
    D(int x) : C(x + 1), mem(bump(x)) { }
    M mem;
};

int main(void) {
    /* three-level chain: 5 -> B(6) -> A(7) */
    C c(5);
    if (c.a != 7)  return 1;   /* A: 5 + 1 + 1 */
    if (c.b != 12) return 2;   /* B: 6 * 2     */
    if (c.c != 15) return 3;   /* C: 5 * 3     */

    /* mixed list: base item + expression-initialized member object */
    D d(5);
    if (d.a != 8)    return 4; /* C(6) -> B(7) -> A(8) */
    if (d.c != 18)   return 5; /* 6 * 3               */
    if (d.mem.m != 15) return 6; /* bump(5) = 15     */

    printf("ctor_init_list_boundary: passed\n");
    return 0;
}
