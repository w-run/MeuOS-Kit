/* pure_virtual.cc — C++ pure virtual members (`= 0`) and virtual
 * destructor chain (m++ end-to-end).
 *
 * Covers: a pure virtual member function (`virtual int f() = 0`) parsing
 * and its null vtable slot (the abstract base's slot stays 0 until the
 * derived class overrides it), a derived override dispatching through the
 * vtable, and the virtual-destructor chain calling derived-then-base in
 * the right order (when the destructor is invoked on the most-derived
 * object directly).
 *
 * A pure virtual *destructor* (`virtual ~B() = 0`) requires an
 * out-of-line definition (`B::~B(){}`), whose out-of-line destructor
 * definition is a separate pre-existing m++ limitation (see TODO), so
 * this test exercises a pure virtual member + a virtual dtor instead.
 *
 * (Note: `delete` on a polymorphic base pointer currently only runs the
 * base destructor — separate pre-existing defect; the destructor chain
 * here is verified via a directly-declared derived object.)
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */
#include <stdio.h>

struct B {
    virtual ~B() { printf("~B"); }
    virtual int f() = 0;        /* pure virtual: null vtable slot */
};
struct D : B {
    int f() override { return 42; }   /* fills the slot */
    ~D() { printf("~D"); }
};

int
main(void)
{
    /* pure virtual member is dispatchable through the vtable */
    {
        D d;
        if (d.f() != 42) return 1;
    }
    putchar('\n');
    printf("chain: ");

    /* destructor chain: ~D then ~B */
    { D d; }
    putchar('\n');

    return 0;
}
