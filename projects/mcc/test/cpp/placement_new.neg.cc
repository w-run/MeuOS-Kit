/* Negative test: placement new `new (ptr) T(args)` must be rejected —
 * m++ reports "expected type in 'new' expression".
 * check-cpp-neg compiles this expecting failure.
 */
class C {
public:
    C() { v = 0; }
    C(int x) { v = x; }
    int v;
};

extern void *malloc(unsigned long);
extern void free(void *);

int main(void) {
    void *buf = malloc(sizeof(C));
    C *p = new (buf) C(9);   /* unsupported: placement new */
    int r = p->v;
    free(buf);
    return r == 9 ? 0 : 1;
}
