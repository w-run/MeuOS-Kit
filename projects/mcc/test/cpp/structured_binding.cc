/* structured_binding.cc — C++17 structured bindings (m++ end-to-end).
 *
 * Covers `auto [a, b] = agg;` binding public struct members, nested
 * bindings (binding a member that is itself an aggregate), and binding
 * inside a range-for / index loop over an array of aggregates.
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */
struct Pair { int x; int y; };

Pair
make_pair(void)
{
    Pair p;
    p.x = 3;
    p.y = 4;
    return p;
}

struct Inner { int a; int b; };
struct Outer { Inner i; int c; };

Outer
make_outer(void)
{
    Outer o;
    o.i.a = 1;
    o.i.b = 2;
    o.c = 3;
    return o;
}

int
main(void)
{
    /* simple binding to a returned struct */
    Pair q = make_pair();
    auto [u, v] = q;
    if (u != 3) return 1;
    if (v != 4) return 2;

    /* nested binding: bind an aggregate member */
    auto [in, c] = make_outer();
    if (in.a != 1) return 3;
    if (in.b != 2) return 4;
    if (c != 3) return 5;

    /* binding inside a loop over an array of aggregates */
    Pair arr[2] = { { 1, 2 }, { 3, 4 } };
    int s = 0;
    for (int i = 0; i < 2; i++) {
        auto [p, q2] = arr[i];
        s += p + q2;
    }
    if (s != 10) return 6;

    return 0;
}
