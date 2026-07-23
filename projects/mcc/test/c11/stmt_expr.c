/* test/c11/stmt_expr.c — GNU statement expression ({...}) support.
 * While not part of ISO C, statement expressions are widely used in
 * real-world code and in community test suites (e.g. chibicc).
 *
 * This test verifies that mcc can parse, compile, and execute:
 *   - simple ({ expr }) 
 *   - ({ decl; expr })
 *   - ({ decl; side_effect_expr; result_expr })
 *   - ({ decl; if/while/for; result_expr })
 *   - nested ({...})
 */
extern int puts(const char *);
extern void exit(int);

static void assert_eq(int expected, int actual, const char *msg) {
    if (expected != actual) {
        puts(msg);
        exit(1);
    }
}

int main(void) {
    /* 1. Simple ({ value }) */
    int a = ({ 42; });
    assert_eq(42, a, "FAIL 1: simple ({})");

    /* 2. ({ decl; value }) */
    int b = ({ int x = 10; x + 5; });
    assert_eq(15, b, "FAIL 2: decl+value");

    /* 3. Side effect + value */
    int c = 0;
    int d = ({ c = 100; c + 1; });
    assert_eq(100, c, "FAIL 3a: side effect");
    assert_eq(101, d, "FAIL 3b: result");

    /* 4. Decl + for loop */
    int sum = ({ int t = 0; for (int i = 0; i < 10; i++) t += i; t; });
    assert_eq(45, sum, "FAIL 4: for loop");

    /* 5. Nested */
    int e = ({ int x = ({ 7; }); x * 2; });
    assert_eq(14, e, "FAIL 5: nested");

    /* 6. Void — no trailing expression */
    ({ puts("PASS 6: void"); });

    puts("PASS");
    return 0;
}
