/*
 * chibicc-style 3-argument assert adapter for the mcc community test suite.
 *
 * chibicc's functional tests declare `void assert(int, int, char *)` in
 * test.h and never include <assert.h>; they expect this helper to be
 * supplied separately (chibicc links its own testutil.c). We provide the
 * definition so each test can be linked with mcc.
 *
 * The adapter is written in a freestanding style (forward-declaring only
 * printf/exit, no <stdio.h>) so it compiles under -nostdinc alongside the
 * chibicc tests, which rely on test.h's own forward declarations. printf /
 * exit resolve to the C library (meuos-libc) at link time and share the
 * x86_64 ABI.
 */
int printf(char *, ...);
void exit(int);

void assert(int expected, int actual, char *code) {
    if (expected != actual) {
        printf("assertion failed: expected %d, got %d  [%s]\n",
               expected, actual, code ? code : "?");
        exit(1);
    }
}
