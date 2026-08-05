/* qualified_threehop.cc — qualified member calls through a three-level
 * inheritance chain (m++ end-to-end).
 *
 * Covers:
 *  - `obj.A::get()` / `obj.B::get()` / `obj.C::get()` on a three-hop
 *    chain A : B : C — each qualification selects that level's method
 *  - qualification with the object's own class at the deepest level
 *  - a qualified call inside a method body (`this->A::get()`)
 *  - qualification through a base-typed object of an intermediate class
 *
 * NOTE: qualifying a *virtual* method currently does not bypass the
 * vtable in m++ (c.A::f() still dispatches virtually); that defect is
 * tracked in .issues/0802.md.  This test uses non-virtual methods.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
extern int printf(const char *, ...);

class A {
public:
    int get() { return 1; }
};

class B : public A {
public:
    int get() { return 2; }
};

class C : public B {
public:
    int get() { return 3; }
    int use_qual() { return this->A::get(); }
};

int main(void) {
    C c;

    /* each level's method reachable by qualifying with that class */
    if (c.A::get() != 1) return 1;
    if (c.B::get() != 2) return 2;
    if (c.C::get() != 3) return 3;

    /* qualification inside a method body (this->A::get) */
    if (c.use_qual() != 1) return 4;

    /* an intermediate-class object qualified to its own base */
    B b;
    if (b.A::get() != 1) return 5;
    if (b.B::get() != 2) return 6;

    printf("qualified_threehop: passed\n");
    return 0;
}
