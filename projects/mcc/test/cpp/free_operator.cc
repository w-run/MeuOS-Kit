/* Non-member operator overloads (C.2.3): `Vec operator+(Vec, Vec)` is a
 * free `operator_pl` function; `a + b` lowers to `operator_pl(a, b)`.
 */
extern int puts(const char *);

class Vec {
public:
    Vec(int v) { m = v; }
    int m;
};

Vec operator+(Vec a, Vec b) { Vec r(a.m + b.m); return r; }

int main(void) {
    Vec a(1), b(2);
    Vec sum = a + b;
    if (sum.m != 3) { puts("FAIL: free operator+"); return 1; }
    puts("PASS");
    return 0;
}
