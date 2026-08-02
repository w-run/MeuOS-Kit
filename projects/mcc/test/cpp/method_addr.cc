/* Taking the address of a member function (C.2.3): `&obj.meth` resolves
 * to the single overloaded mangled function `Class_meth_...`; a class
 * with multiple overloads must reject the unqualified address as
 * ambiguous (verified by test/cpp/ambig_addr.neg.cc).
 */
extern int puts(const char *);

class Calc {
public:
    int add(int a, int b) { return a + b; }
};

int main(void) {
    Calc c;
    void *p = &c.add;
    if (!p) { puts("FAIL: &c.add"); return 1; }
    puts("PASS");
    return 0;
}
