/* defect_h_threehop.cc — class-qualified virtual calls across a three-hop
 * inheritance chain must each statically bind to the named class.
 *
 * A <- B <- C, each overriding virtual f().  `c.A::f()`, `c.B::f()` and
 * `c.C::f()` name three distinct implementations; the `X::` qualification
 * suppresses virtual dispatch (defect H), so each must reach exactly the
 * implementation it names rather than C::f (the most-derived override).
 *
 * Unqualified/base-pointer calls are re-checked here to make sure the
 * static-binding guard did not disable virtual dispatch altogether.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
extern int printf(const char *, ...);

class A {
public:
    virtual int f() { return 1; }
};

class B : public A {
public:
    virtual int f() { return 2; }
};

class C : public B {
public:
    virtual int f() { return 3; }
};

int main(void) {
    C c;

    /* each qualified call binds statically to the class it names */
    if (c.A::f() != 1) return 1;
    if (c.B::f() != 2) return 2;
    if (c.C::f() != 3) return 3;

    printf("A::f=%d B::f=%d C::f=%d\n", c.A::f(), c.B::f(), c.C::f());

    /* virtual dispatch still works for unqualified calls */
    if (c.f() != 3) return 4;

    /* ...and through base-class pointers at every level */
    A *pa = &c;
    B *pb = &c;
    if (pa->f() != 3) return 5;
    if (pb->f() != 3) return 6;

    /* a qualified call through a base pointer names the base version */
    if (pa->A::f() != 1) return 7;
    if (pb->B::f() != 2) return 8;

    printf("defect_h_threehop: passed\n");
    return 0;
}
