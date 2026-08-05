/* vfun_combos.cc — C.2.5 inheritance + virtual-function combination cases
 * (m++ end-to-end).
 *
 * Extends virtual.cc with boundary combinations:
 *  - a virtual member that calls another virtual member (virtual dispatch
 *    through `this` happens even from inside a base-class body)
 *  - deep inheritance chains (4 levels) with per-level overrides
 *  - virtuals returning double / bool / with parameters
 *  - a polymorphic class whose base is non-polymorphic (vptr insertion)
 *  - virtual calls on objects passed by reference
 *  - polymorphic objects stored via base-class reference variables
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */

/* --- virtual member calling another virtual member ---
 * Base::calc calls vf() through `this`; a derived object inherits calc()
 * but overrides vf(), so the inner call must dispatch dynamically. */
class Base {
public:
    virtual int vf(int x) { return x + 1; }
    int calc(int x) { return vf(x) * 2; }
};
class Der : public Base {
public:
    int vf(int x) { return x + 100; }
};
class Der2 : public Der {
public:
    int vf(int x) { return x + 1000; }
};

/* --- deep chain with mixed override points --- */
class L1 {
public:
    virtual int f(int x) { return x + 1; }
    virtual int g(int x) { return x * 2; }
};
class L2 : public L1 {
public:
    int f(int x) { return x + 10; }   /* override only f */
};
class L3 : public L2 {
public:
    int g(int x) { return x * 20; }   /* override only g */
};
class L4 : public L3 {
public:
    int f(int x) { return x + 1000; } /* override f again */
};

/* --- virtuals with double / bool return and multiple params --- */
class Num {
public:
    virtual double scale(double d) { return d; }
    virtual bool isbig(int x) { return x > 100; }
    virtual int add3(int a, int b, int c) { return a + b + c; }
};
class Num2 : public Num {
public:
    double scale(double d) { return d * 1.5; }
    bool isbig(int x) { return x > 10; }
};

/* --- polymorphic class whose direct base is non-polymorphic --- */
class Plain {
public:
    int base;
    Plain() { base = 3; }
};
class Poly : public Plain {
public:
    virtual int get() { return base; }
};
class Poly2 : public Poly {
public:
    int get() { return base * 10; }
};

/* --- virtual calls through reference variables / by-ref params --- */
class Animal {
public:
    virtual int legs() { return 0; }
};
class Dog : public Animal {
public:
    int legs() { return 4; }
};
int count_legs(Animal &a) { return a.legs(); }   /* by-ref param */
class Bird : public Animal {
public:
    int legs() { return 2; }
};

/* --- constructor chain + virtual reading derived data --- */
class Shape {
public:
    int dim;
    Shape(int d) { dim = d; }
    virtual int area() { return 0; }
};
class Sq : public Shape {
public:
    Sq(int d) { dim = d; }
    int area() { return dim * dim; }
};
class Cube : public Sq {
public:
    Cube(int d) { dim = d; }
    int area() { return dim * dim * 6; }
};

int main(void) {
    /* virtual-in-virtual dispatch */
    Base b1;
    if (b1.calc(5) != 12) return 1;       /* (5+1)*2 */
    Der d1;
    if (d1.calc(5) != 210) return 2;      /* (5+100)*2 */
    Base *pb = &d1;
    if (pb->calc(5) != 210) return 3;     /* dynamic through base ptr */
    Der2 d2;
    if (d2.calc(5) != 2010) return 4;     /* deepest override */

    /* deep chain */
    L1 o1;
    if (o1.f(1) != 2) return 5;
    if (o1.g(3) != 6) return 6;
    L2 o2;
    if (o2.f(1) != 11) return 7;
    if (o2.g(3) != 6) return 8;           /* g inherited from L1 */
    L3 o3;
    L1 *p13 = &o3;
    if (p13->f(1) != 11) return 9;        /* L2::f */
    if (p13->g(3) != 60) return 10;       /* L3::g */
    L4 o4;
    L1 *p14 = &o4;
    if (p14->f(1) != 1001) return 11;     /* L4::f */
    if (p14->g(3) != 60) return 12;       /* L3::g still */

    /* double/bool/3-param virtuals */
    Num n1;
    if (n1.scale(2.0) != 2.0) return 13;
    Num2 n2;
    Num *pn = &n2;
    if (pn->scale(2.0) != 3.0) return 14;
    if (!pn->isbig(50)) return 15;        /* Num2::isbig, 50 > 10 */
    if (pn->add3(1, 2, 3) != 6) return 16; /* inherited, non-virtual here */

    /* non-polymorphic base */
    Poly2 p2;
    Poly *pp = &p2;
    Plain *pplain = &p2;
    if (pplain->base != 3) return 17;
    if (pp->get() != 30) return 18;

    /* by-ref param + reference variable */
    Dog dog;
    Bird bird;
    if (count_legs(dog) != 4) return 19;
    if (count_legs(bird) != 2) return 20;
    Animal &ra = bird;
    if (ra.legs() != 2) return 21;

    /* ctor chain + virtual reading derived data */
    Sq sq(4);
    Cube cb(3);
    Shape *ps = &sq;
    if (ps->area() != 16) return 22;
    ps = &cb;
    if (ps->area() != 54) return 23;
    if (cb.area() != 54) return 24;

    return 0;
}
