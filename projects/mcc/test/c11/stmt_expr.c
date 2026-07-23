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
#include <stdio.h>
#include <stdlib.h>

/* Minimal assert without test.h to keep it self-contained */
static int failures;
#define expect(exp, actual, code) do { \
    long e = (long)(exp), a = (long)(actual); \
    if (e != a) { \
        printf("FAIL: expected %ld, got %ld  [%s]\n", e, a, code); \
        failures++; \
    } \
} while(0)

int main(void)
{
    /* 1. Simple value */
    expect(42, ({ 42; }), "simple");

    /* 2. Decl + value */
    expect(5, ({ int x = 5; x; }), "decl-value");

    /* 3. Decl + side-effect + value */
    expect(7, ({ int x = 5; x += 2; x; }), "side-effect");

    /* 4. Control flow inside */
    int r = ({ int x = 1; if (x) x = 10; else x = 20; x; });
    expect(10, r, "if-inside");

    /* 5. For loop inside */
    r = ({ int s = 0; for (int i = 0; i < 10; i++) s += i; s; });
    expect(45, r, "for-inside");

    /* 6. Nested */
    r = ({ int a = ({ int b = 3; b; }); a * a; });
    expect(9, r, "nested");

    /* 7. Void (no last expr) */
    ({ int x = 1; x = x + 1; });

    if (failures)
        printf("FAILED (%d)\n", failures);
    else
        printf("PASS\n");
    return failures;
}
