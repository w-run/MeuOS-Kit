/* class_tmpl_requires.cc — a class template carrying a requires-clause
 * (C++20 constrained class template, m++).
 *
 *   template<typename T> requires C<T> class Foo { ... };
 *
 * The class-key follows the requires-clause rather than the '>' of the
 * template parameter list, so the parser must consume the clause before
 * deciding the template declares a class; otherwise the trailing ';' of
 * `};` is left in the stream and rejected at top level.
 *
 * Covers `requires true` (literal constraint), a named concept, and both
 * the `class` and `struct` class-keys.
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */
template <typename T> concept Int4 = sizeof(T) == 4;

/* literal constraint, `class` key */
template <typename T> requires true class C { public: int x; };

/* named concept, `class` key */
template <typename T> requires Int4<T> class D { public: T y; };

/* named concept, `struct` key */
template <typename T> requires Int4<T> struct S { T z; };

int
main(void)
{
    C<int> c;
    c.x = 1;
    if (c.x != 1) return 1;

    D<int> d;
    d.y = 2;
    if (d.y != 2) return 2;

    S<int> s;
    s.z = 3;
    if (s.z != 3) return 3;

    if (c.x + d.y + s.z != 6) return 4;

    return 0;
}
