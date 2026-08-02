/* struct_multinher.cc — struct multiple inheritance (m++ end-to-end).
 *
 * Covers:
 *  - `struct D : A, B` multi-inheritance with a method body (top-level)
 *  - access specifiers in the base list (`public A, protected B`)
 *  - a struct derived from a derived struct (chain)
 *  - nested struct multi-inheritance inside a class body
 *  - struct multi-inheritance inside a namespace
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
extern int printf(const char *, ...);

struct A { int x; };
struct B { int y; };
struct D : public A, protected B {
    void set(int v) { x = v; y = v * 2; }
    int sum() { return x + y; }
};

struct E : D {
    void sete(int v) { z = v; }
    int z;
};

class Outer {
public:
    struct I1 { int a; };
    struct I2 { int b; };
    struct Both : I1, I2 {
        void setv(int v) { a = v; b = v + 1; }
        int tot() { return a + b; }
    };
};

namespace NS {
    struct N1 { int p; };
    struct N2 { int q; };
    struct ND : N1, N2 {
        void setp(int v) { p = v; q = v + 3; }
        int pq() { return p + q; }
    };
}

int main(void) {
    /* top-level struct multi-inheritance */
    D d;
    d.set(5);
    if (d.sum() != 15) return 1;
    if (d.x != 5) return 2;

    /* struct chain: derived of derived */
    E e;
    e.set(1);
    e.sete(9);
    if (e.z != 9) return 3;
    if (e.sum() != 3) return 4;

    /* nested struct multi-inheritance inside a class */
    Outer::Both b;
    b.setv(7);
    if (b.tot() != 15) return 5;

    /* struct multi-inheritance inside a namespace */
    NS::ND n;
    n.setp(10);
    if (n.pq() != 23) return 6;

    printf("struct_multinher: passed\n");
    return 0;
}
