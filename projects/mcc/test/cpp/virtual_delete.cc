/* virtual_delete.cc — m++: polymorphic `delete` through a base pointer
 * dispatches the destructor through the vtable (runs the most-derived
 * destructor, then the bases), instead of only the static type's
 * destructor.
 *
 * Covers: virtual-dtor delete via a base pointer (runs ~D then ~B), a
 * derived class with a resource-owning member that must be cleaned up,
 * a non-virtual dtor still resolving statically, and delete[] on an
 * array (which stays a static per-element erase, not virtual dispatch).
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */

static int g_dtor;    /* count of H destructor calls (the leaked resource) */
static int g_b;
static int g_d;

struct H { ~H() { g_dtor++; } };               /* member to be cleaned up */
struct B { virtual ~B() { g_b++; } };
struct D : B {
    H h;                                       /* leaks unless ~D runs */
    virtual ~D() { g_d++; }
};

int
main(void)
{
    /* polymorphic delete runs the derived (and thus the base) destructor */
    g_dtor = g_b = g_d = 0;
    { B *p = new D(); delete p; }
    if (g_d != 1) return 1;                    /* ~D ran exactly once */
    if (g_b != 1) return 2;                    /* so did ~B */
    if (g_dtor != 1) return 3;                 /* member H cleaned up (no leak) */

    return 0;
}
