/* [[nodiscard]] / [[deprecated]] attribute semantics.
 *
 * - [[nodiscard]]: the attribute is accepted; discarding the return of a
 *   nodiscard function in an expression statement emits a warning (see
 *   stmt.c) without failing the build.
 * - [[deprecated]]: the attribute (optionally with a message) is accepted
 *   and the entity remains usable.
 * Returns 0 on success. */
[[nodiscard]] int make_val(void) { return 7; }

[[deprecated("use make_val() instead")]]
int legacy_val(void) { return 3; }

int main(void) {
    int a = make_val();           /* result used: no warning */
    if (a != 7) return 1;

    int b = legacy_val();         /* deprecated but callable */
    if (b != 3) return 2;

    return 0;
}
