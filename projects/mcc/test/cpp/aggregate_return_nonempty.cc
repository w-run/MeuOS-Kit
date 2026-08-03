/* aggregate_return_nonempty.cc — non-empty class by-value return matrix.
 *
 * C++ regression for "D4: non-empty class by-value return broken" (P0).
 * Sized matrix covering the SysV register-return window (≤16B) and the
 * memory-class return path (>16B, sret hidden pointer) on x86_64.
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */
extern int puts(const char *);

/* 8B — two ints, register-class */
class C8 {
public:
    C8(int x, int y) { a = x; b = y; }
    int a, b;
};
C8 mk8(int v) { return C8(v, v + 1); }

/* 12B — three ints, register-class (RAX + RDX) */
class C12 {
public:
    C12(int x, int y, int z) { a = x; b = y; c = z; }
    int a, b, c;
};
C12 mk12(int v) { return C12(v, v + 1, v + 2); }

/* 16B — {int,double}, register-class (RAX + XMM0) */
class C16 {
public:
    C16(int x, double d) { n = x; v = d; }
    int n;
    double v;
};
C16 mk16(int n, double v) { return C16(n, v); }

/* 24B — three longs, sret */
class C24 {
public:
    C24(long p, long q, long r) { a = p; b = q; c = r; }
    long a, b, c;
};
C24 mk24(long v) { C24 s; s.a = v; s.b = v + 1; s.c = v + 2; return s; }

/* 40B — five longs, sret */
class C40 {
public:
    long a, b, c, d, e;
};
C40 mk40(long v) { C40 s; s.a = v; s.b = v+1; s.c = v+2; s.d = v+3; s.e = v+4; return s; }

/* pass-through: take a non-empty by-value, return it untouched */
C24 passthru24(C24 x) { return x; }

int
main(void)
{
    C8 c8 = mk8(1);
    if (c8.a != 1 || c8.b != 2) { puts("FAIL: c8"); return 1; }

    C12 c12 = mk12(10);
    if (c12.a != 10 || c12.b != 11 || c12.c != 12) { puts("FAIL: c12"); return 2; }

    C16 c16 = mk16(7, 0.5);
    if (c16.n != 7) { puts("FAIL: c16.n"); return 3; }
    if (c16.v != 0.5) { puts("FAIL: c16.v"); return 4; }

    C24 c24 = mk24(20);
    if (c24.a != 20 || c24.b != 21 || c24.c != 22) { puts("FAIL: c24"); return 5; }

    C40 c40 = mk40(30);
    if (c40.a != 30 || c40.b != 31 || c40.c != 32) { puts("FAIL: c40a-c"); return 6; }
    if (c40.d != 33 || c40.e != 34) { puts("FAIL: c40d-e"); return 7; }

    /* pass-through exercises both sret arg (hidden) and sret return (hidden) */
    C24 pas = passthru24(mk24(50));
    if (pas.a != 50 || pas.b != 51 || pas.c != 52) { puts("FAIL: passthru24"); return 8; }

    puts("PASS");
    return 0;
}