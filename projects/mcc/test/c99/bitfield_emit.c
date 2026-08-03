/* Regression: static (emitdata) bit-field initializers must pack the
 * fields into the shared storage unit without clobbering an earlier
 * non-bit-field member that shares the unit.
 *
 * chibicc: `struct { char a; int b:5; int c:10; } g = {1,2,3};` — b and c
 * live in a 4-byte unit that starts at byte 0, overlapping a's byte.
 * funcstore() inserts bits; emitdata previously emitted the whole packed
 * unit and wiped a.  Verify global + local agree.
 */
extern int puts(const char *);

struct S { char a; int b : 5; int c : 10; };
struct S g45 = {1, 2, 3};

int main(void) {
    struct S x = {1, 2, 3};
    if (g45.a != 1) { puts("FAIL: g45.a"); return 1; }
    if (g45.b != 2) { puts("FAIL: g45.b"); return 2; }
    if (g45.c != 3) { puts("FAIL: g45.c"); return 3; }
    if (x.a != 1 || x.b != 2 || x.c != 3) { puts("FAIL: local"); return 4; }

    /* a signed bit-field initializer that needs sign extension */
    struct { int d : 5; int e : 10; } neg = { -1, -3 };
    if (neg.d != -1) { puts("FAIL: neg.d"); return 5; }
    if (neg.e != -3) { puts("FAIL: neg.e"); return 6; }

    return 0;
}
