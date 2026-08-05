/* out_of_line_dtor.cc — m++: out-of-line class destructor definition
 * `B::~B(){}` (C++), enabling the full pure-virtual-destructor object
 * lifecycle.
 *
 * A pure virtual destructor (`virtual ~B() = 0`) still requires an
 * out-of-line definition; this test covers that definition form plus the
 * derived destructor chain.  Each check returns a distinct exit code;
 * run via `check-cpp-func`.
 */

struct B {
    virtual ~B() = 0;          /* pure virtual destructor */
    virtual int f() = 0;       /* pure virtual member */
};
B::~B() {}                     /* out-of-line definition */
struct D : B {
    int f() override { return 42; }
    virtual ~D() {}
};

int
main(void)
{
    /* a derived object of a class with a pure virtual base destructor */
    { D d; if (d.f() != 42) return 1; }
    return 0;
}
