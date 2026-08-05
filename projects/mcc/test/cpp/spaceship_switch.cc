/* spaceship_switch.cc — C++20 three-way comparison `<=>` with switch
 * (m++ end-to-end).
 *
 * `<=>` folds to -1/0/+1, so it can drive a switch directly:
 *   switch (a <=> b) { case -1: ... case 0: ... case 1: ... }
 *
 * Covers:
 *  - the result of `<=>` used as a switch selector
 *  - all three branches reachable from literal and variable operands
 *  - the selector in a switch with a default guard
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
extern int printf(const char *, ...);

static int cmp(int a, int b) {
    switch (a <=> b) {
    case -1: return -1;
    case 0:  return 0;
    case 1:  return 1;
    }
    return 99;   /* unreachable for valid ints */
}

int
main(void)
{
    /* all three outcomes of the spaceship switch */
    if (cmp(3, 5) != -1) return 1;
    if (cmp(5, 5) != 0)  return 2;
    if (cmp(7, 5) != 1)  return 3;

    /* selector computed from variables, not literals */
    int a = 10, b = 4;
    switch (a <=> b) {
    case 1:  break;
    default: return 4;
    }
    switch (b <=> a) {
    case -1: break;
    default: return 5;
    }
    switch (a <=> a) {
    case 0:  break;
    default: return 6;
    }

    printf("spaceship_switch: passed\n");
    return 0;
}
