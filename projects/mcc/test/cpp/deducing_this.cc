/* deducing_this.cc — C++23 explicit object parameter `this X& self`
 * (P0847), m++.  Reference forms.
 *
 * Covers:
 *   - `this X& self`: read + write through the object parameter
 *   - `this const X& self`: const object (const method mangling "K")
 *   - `this X&& self`: temporary object (rvalue overload mangling "V")
 *   - explicit arguments alongside the object parameter
 *   - value-category overload resolution: `x.f()` -> X& form,
 *     `X(..).f()` -> X&& form when both are declared
 *   - bare member access (no `self.` prefix) and `this->` inside the body
 *   - regression: ordinary member methods still work, and const
 *     overloaded member methods with arguments resolve (K placement)
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
extern int printf(const char *, ...);

struct Counter {
    int n;
    Counter() { n = 0; }
    Counter(int v) { n = v; }

    /* `this X& self` — read */
    int get(this Counter& self) { return self.n; }
    /* `this X& self` — write back to the object */
    void set(this Counter& self, int v) { self.n = v; }
    /* bare member access through the object param */
    int bare(this Counter& self) { return n; }
    /* `this` still denotes the object pointer */
    int via_this(this Counter& self) { return this->n; }
    /* `this const X& self` — const object */
    int cget(this const Counter& self) { return self.n; }
    /* explicit args alongside the object parameter */
    int add(this Counter& self, int a, int b) { return self.n + a + b; }

    /* ordinary member methods must keep working */
    int plain() { return n * 10; }
    int plainc(int a) const { return n + a; }

    /* const overloaded member with args (K-suffix resolution fix) */
    int pick(int a) { return n + a; }
    int pick(int a) const { return n + a + 1000; }
};

/* value-category overloads: lvalue object -> X& form, temporary -> X&& */
struct Vec {
    int v;
    Vec(int x) { v = x; }
    int f(this Vec& self) { return self.v; }
    int f(this Vec&& self) { return self.v + 100; }
};

int
main(void)
{
    Counter c;
    c.n = 7;

    if (c.get() != 7) return 1;
    c.set(9);
    if (c.n != 9) return 2;
    if (c.bare() != 9) return 3;
    if (c.via_this() != 9) return 4;

    const Counter& cc = c;
    if (cc.cget() != 9) return 5;

    if (c.add(1, 2) != 12) return 6;

    /* ordinary members still work */
    if (c.plain() != 90) return 7;
    if (cc.plainc(1) != 10) return 8;

    /* const overloaded member with args (non-const and const object) */
    if (c.pick(1) != 10) return 9;
    if (cc.pick(1) != 1010) return 10;

    /* rvalue-object (`this X&& self`) */
    Vec t(5);
    if (t.f() != 5) return 11;
    if (Vec(3).f() != 103) return 12;

    /* `this const X&& self` via a temporary through a const-ref is an
     * edge; keep the lvalue/const and rvalue paths covered above */

    printf("deducing_this: all passed\n");
    return 0;
}
