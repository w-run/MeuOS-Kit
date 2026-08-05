/* attr_basic.cc — C++11/17 attribute syntax (m++ end-to-end).
 *
 * m++ parses and (for the supported positions) accepts standard
 * attributes.  At HEAD (eb8372d) the accepted positions are:
 *  - [[maybe_unused] at file/namespace scope  (NOT on locals/params yet)
 *  - [[nodiscard]] on a function declaration
 *  - [[fallthrough]] as a switch-case statement attribute
 * This test exercises exactly those accepted positions and confirms they
 * do not break compilation or execution.
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */
[[maybe_unused]] int unused_global = 42;

[[nodiscard]] int
next(int x)
{
    return x + 1;
}

int
main(void)
{
    /* unused_global is intentionally unused; [[maybe_unused]] keeps it
     * accepted at file scope */

    if (next(3) != 4) return 1;

    int v = 0;
    switch (v) {
    case 0:
        v = 10;
        [[fallthrough]];
    case 1:
        v += 1;
        break;
    default:
        return 2;
    }
    if (v != 11) return 3;

    return 0;
}
