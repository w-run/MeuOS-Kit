/* qualified_member.cc — class-qualified member calls (m++ end-to-end).
 *
 * Covers:
 *  - `obj.Base::get()` — a class-qualified call that forces the base
 *    class's implementation of a (possibly overridden) method
 *  - `obj.Base::val` — qualified data-member access through the object
 *  - a qualified call inside a derived-class method body (this->Base::get)
 *  - a qualified call through a base-class-typed expression (two-level
 *    derived chain: Der : Mid : Base)
 *  - `obj.Own::get()` — qualification with the object's own class
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
extern int printf(const char *, ...);

class Base {
public:
    int get()      { return 10; }
    int val;
};

class Mid : public Base {
public:
    int get()      { return 20; }   /* hides Base::get */
};

class Der : public Mid {
public:
    int get()      { return 30; }   /* hides Mid::get */
    int use_qual() { return this->Base::get(); }
};

int main(void) {
    Der d;
    d.val = 5;

    /* class-qualified call forces the base implementation, skipping
     * the Mid::get and Der::get overrides */
    if (d.Base::get() != 10) return 1;

    /* qualified data-member access */
    if (d.Base::val != 5) return 2;

    /* qualified call inside a derived method body (this->Base::get) */
    if (d.use_qual() != 10) return 3;

    /* qualified call through a base-class-typed expression */
    Mid m;
    if (m.Base::get() != 10) return 4;

    /* qualification with the object's own class */
    if (m.Mid::get() != 20) return 5;

    printf("qualified_member: passed\n");
    return 0;
}
