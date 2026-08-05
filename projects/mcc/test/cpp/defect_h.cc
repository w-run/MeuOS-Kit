/* defect_h.cc — class-qualified virtual calls must bypass the vtable
 * (defect H regression, m++ end-to-end).
 *
 * A class-qualified call `obj.Base::f()` where `f` is virtual must
 * statically bind to `Base::f`; before the fix it wrongly dispatched
 * through the vtable to the most-derived override (c.A::f() returned 3
 * instead of 1).
 *
 * Covers:
 *  - `c.A::f()` / `c.B::f()` on a three-hop A:B:C chain bind statically
 *  - unqualified `c.f()` and base-pointer `p->f()` still dispatch
 *    virtually to the most-derived override
 *  - a qualified call inside a method body (`this->A::f()`)
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
    int use_qual() { return this->A::f(); }
};

int main(void) {
    C c;

    /* qualified calls statically bind to the named class */
    if (c.A::f() != 1) return 1;
    if (c.B::f() != 2) return 2;

    /* unqualified calls still dispatch virtually */
    if (c.f() != 3) return 3;
    A *p = &c;
    if (p->f() != 3) return 4;

    /* qualified call inside a method body */
    if (c.use_qual() != 1) return 5;

    printf("defect_h: passed\n");
    return 0;
}
