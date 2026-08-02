/* virtual.cc — C.2.5 virtual functions & vtables (m++ end-to-end).
 *
 * Covers: base-class vtable layout, override slot reuse, dispatch through
 * a base-class pointer, multi-level inheritance, parameterized virtual
 * calls, virtual calls from inside a member function, multiple inheritance
 * with two polymorphic bases (secondary vtables), and a polymorphic class
 * whose direct base is non-polymorphic (hidden vptr insertion).
 *
 * Each check returns a distinct exit code; run with `check-cpp-virtual`.
 */
#include <stdio.h>

/* --- single inheritance / override / base-pointer dispatch --- */
class Shape {
public:
    virtual int area() { return 0; }
    virtual const char *name() { return "Shape"; }
};
class Rect : public Shape {
public:
    int w, h;
    Rect(int w_, int h_) { w = w_; h = h_; }
    int area() { return w * h; }
    const char *name() { return "Rect"; }
};
class Circle : public Shape {
public:
    int r;
    Circle(int r_) { r = r_; }
    int area() { return r * r * 3; }
    const char *name() { return "Circle"; }
};

/* --- parameterized virtual + multi-level inheritance + in-method call --- */
class Base {
public:
    virtual int calc(int x, int y) { return x + y; }
};
class Mid : public Base {
public:
    int calc(int x, int y) { return x * y; }
};
class Leaf : public Mid {
public:
    int calc(int x, int y) { return x - y; }
    int helper() {
        /* virtual dispatch through `this` from inside a member function */
        return calc(10, 3);
    }
};

/* --- multiple inheritance: two polymorphic bases (secondary vtable) --- */
class A {
public:
    virtual int fa() { return 1; }
};
class B {
public:
    virtual int fb() { return 2; }
};
class D : public A, public B {
public:
    int fa() { return 10; }
    int fb() { return 20; }
};

/* --- polymorphic class with a non-polymorphic direct base --- */
class Plain {
public:
    int x;
    Plain() { x = 5; }
};
class Poly : public Plain {
public:
    virtual int get() { return x + 1; }
};
class Sub : public Poly {
public:
    int get() { return x + 100; }
};

int
main(void)
{
    Rect rc(4, 5);
    Circle ci(7);
    Shape *s1 = &rc;
    Shape *s2 = &ci;
    if (s1->area() != 20) return 1;
    if (s2->area() != 147) return 2;
    if (s1->name()[0] != 'R') return 3;
    if (s2->name()[0] != 'C') return 4;
    if (rc.area() != 20) return 5;
    Shape sp;
    if (sp.area() != 0) return 6;

    Leaf leaf;
    Mid mid;
    Base *b1 = &leaf;
    Base *b2 = &mid;
    if (b1->calc(10, 3) != 7) return 7;
    if (b2->calc(10, 3) != 30) return 8;
    if (leaf.helper() != 7) return 9;

    D d;
    A *a = &d;
    B *b = &d;
    if (a->fa() != 10) return 10;
    if (b->fb() != 20) return 11;
    if (d.fa() != 10) return 12;
    if (d.fb() != 20) return 13;

    Poly p;
    Sub s;
    Plain *pp = &p;
    if (pp->x != 5) return 14;
    if (p.get() != 6) return 15;
    if (s.get() != 105) return 16;
    Poly *p2 = &s;
    if (p2->get() != 105) return 17;

    printf("virtual.cc: all checks passed\n");
    return 0;
}
