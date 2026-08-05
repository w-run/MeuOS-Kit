/* C++23 P1401: `if constexpr` condition narrowing to bool.
 *
 * The condition is contextually converted to bool, so pointer / nullptr
 * constants are accepted (`if constexpr (p)` for a pointer constant p),
 * while a non-constant runtime pointer is still rejected (see the .neg.cc
 * companion).  Returns 0 on success. */
int pick_nullptr(void) {
    if constexpr (nullptr)       /* false: nullptr converts to 0 */
        return 1;
    return 2;
}

int pick_addr(void) {
    if constexpr ((int*)1)       /* true: a non-null pointer constant */
        return 3;
    return 4;
}

int pick_int(void) {
    if constexpr (1)             /* true: plain int constant */
        return 5;
    return 6;
}

int main(void) {
    if (pick_nullptr() != 2) return 1;
    if (pick_addr() != 3) return 2;
    if (pick_int() != 5) return 3;
    return 0;
}
