/* defect_h_this_arrow.cc — qualified virtual calls via `this->Base::f()`
 * inside a member body must statically bind to the named class.
 *
 * The qualification path runs on the postfix `->`/`.` member access, so a
 * call written against the implicit `this` object has to honour it too:
 * `this->A::f()` in a C member must reach A::f, not the C override that
 * virtual dispatch would select (defect H).
 *
 * Also covers the "skip one level" case (`this->A::f()` from C, where B
 * sits in between) and calling a qualified virtual from a base-class body,
 * where `this` dynamically points at a derived object.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
extern int printf(const char *, ...);

class A {
public:
    virtual int f() { return 1; }
    /* `this` is dynamically a C here, but the qualification pins A::f */
    int from_base_qual() { return this->A::f(); }
    /* unqualified: must still dispatch virtually to the override */
    int from_base_virt() { return this->f(); }
};

class B : public A {
public:
    virtual int f() { return 2; }
};

class C : public B {
public:
    virtual int f() { return 3; }
    int qual_a() { return this->A::f(); }   /* skips B and C */
    int qual_b() { return this->B::f(); }   /* skips C */
    int qual_c() { return this->C::f(); }   /* names own class */
    int unqual() { return this->f(); }      /* virtual -> C::f */
};

int main(void) {
    C c;

    /* qualified calls on `this` bind statically */
    if (c.qual_a() != 1) return 1;
    if (c.qual_b() != 2) return 2;
    if (c.qual_c() != 3) return 3;

    /* unqualified call on `this` still dispatches virtually */
    if (c.unqual() != 3) return 4;

    /* qualified call from a base-class body, `this` is really a C */
    if (c.from_base_qual() != 1) return 5;
    /* unqualified from the same base body must reach the C override */
    if (c.from_base_virt() != 3) return 6;

    /* same two calls reached through a base pointer */
    A *pa = &c;
    if (pa->from_base_qual() != 1) return 7;
    if (pa->from_base_virt() != 3) return 8;

    printf("defect_h_this_arrow: passed\n");
    return 0;
}
