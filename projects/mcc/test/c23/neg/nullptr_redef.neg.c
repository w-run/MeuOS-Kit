/* NEGATIVE C23 test: `nullptr` is a reserved keyword (6.4.1); it cannot be
 * redeclared as an ordinary identifier.
 *
 * Expected: mcc error. Verified by run-neg.sh expecting non-zero compile exit.
 */
int nullptr = 5;        /* redefinition of keyword */

int main(void) { return 0; }
