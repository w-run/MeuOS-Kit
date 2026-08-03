/* NEGATIVE test: C 6.10.3p2 — redefining a macro with a replacement list
 * that differs from the current definition is a constraint violation and
 * must be diagnosed.  Here DIFF is defined as 1 and then as 2.
 *
 * Expected: mcc error ("'DIFF' redefined with a different replacement list").
 * Verified by run-neg.sh expecting a non-zero compile exit.
 */
#define DIFF 1
#define DIFF 2

int main(void) { return 0; }
