/* C++11 inherited constructors: `using Base::Base;` synthesizes derived
 * constructors that forward every parameter to the base constructors.
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
struct B {
    int n;
    B() : n(100) {}
    B(int x) : n(x) {}
    B(int x, int y) : n(x + y) {}
};

struct D : B {
    using B::B;
};

class C {
public:
    int v;
    C(int x) : v(x * 2) {}
};

class E : public C {
public:
    using C::C;
};

int main(void) {
    D d1(5);
    if (d1.n != 5) return 1;        /* inherited single-arg ctor */
    D d2(5, 7);
    if (d2.n != 12) return 2;       /* inherited two-arg ctor */
    D d0;
    if (d0.n != 100) return 3;      /* inherited default ctor */
    E e(9);
    if (e.v != 18) return 4;        /* inherited ctor through a class base */
    return 0;
}
