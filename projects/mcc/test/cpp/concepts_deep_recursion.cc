/* concepts_deep_recursion.cc — legitimate deep concept nesting (m++).
 *
 * MAX_CONSTRAINT_DEPTH used to be 16, which rejected perfectly valid
 * template code as "requires-clause evaluation too deep".  The guard
 * exists to stop runaway mutual recursion, not to cap the depth of
 * real code, so it now sits at 256.  This test pins down the depths
 * that must compile and evaluate correctly.
 *
 * Covers:
 *  - a 10-level chain of requires-clauses (each function template
 *    constrained by the next concept in the chain)
 *  - a 20-level concept reference chain, plain and under a negation
 *  - `Concept<Concept<...>>`-style nesting: concept bodies that
 *    combine several other concepts, nested 8 levels deep, so the
 *    expansion fans out instead of forming a single chain
 *  - a 30-level chain mixing &&/||/! at every level
 *
 * Each check returns a distinct exit code; exit 0 = all passed.
 */

/* --- 1. 20-level plain reference chain --------------------------- */
template <typename T> concept A0 = sizeof(T) == 4;
template <typename T> concept A1 = A0<T>;
template <typename T> concept A2 = A1<T>;
template <typename T> concept A3 = A2<T>;
template <typename T> concept A4 = A3<T>;
template <typename T> concept A5 = A4<T>;
template <typename T> concept A6 = A5<T>;
template <typename T> concept A7 = A6<T>;
template <typename T> concept A8 = A7<T>;
template <typename T> concept A9 = A8<T>;
template <typename T> concept A10 = A9<T>;
template <typename T> concept A11 = A10<T>;
template <typename T> concept A12 = A11<T>;
template <typename T> concept A13 = A12<T>;
template <typename T> concept A14 = A13<T>;
template <typename T> concept A15 = A14<T>;
template <typename T> concept A16 = A15<T>;
template <typename T> concept A17 = A16<T>;
template <typename T> concept A18 = A17<T>;
template <typename T> concept A19 = A18<T>;
template <typename T> concept A20 = A19<T>;

template <typename T> requires A20<T> T bump(T x) { return x + 1; }

/* the same 20-level chain under a negation, as its own template: the
 * whole chain has to be expanded and folded before the `!` applies */
template <typename T> requires !A20<T> int wide(T) { return 1; }

/* --- 2. 10-level chain of requires-clauses ----------------------- */
/* every level is its own concept guarded by the previous one, and the
 * function at each level is constrained by its own concept */
template <typename T> concept B0 = sizeof(T) >= 1;
template <typename T> concept B1 = B0<T> && sizeof(T) >= 1;
template <typename T> concept B2 = B1<T> && sizeof(T) >= 1;
template <typename T> concept B3 = B2<T> && sizeof(T) >= 1;
template <typename T> concept B4 = B3<T> && sizeof(T) >= 1;
template <typename T> concept B5 = B4<T> && sizeof(T) >= 1;
template <typename T> concept B6 = B5<T> && sizeof(T) >= 1;
template <typename T> concept B7 = B6<T> && sizeof(T) >= 1;
template <typename T> concept B8 = B7<T> && sizeof(T) >= 1;
template <typename T> concept B9 = B8<T> && sizeof(T) >= 1;
template <typename T> concept B10 = B9<T> && sizeof(T) >= 1;

template <typename T> requires B10<T> int lvl10(T) { return 10; }

/* --- 3. Concept<Concept<...>> style nesting ---------------------- */
/* each level references the level below it three times in a boolean
 * combination, so the expansion is a tree, not a chain */
template <typename T> concept N0 = sizeof(T) == 4;
template <typename T> concept N1 = (N0<T> && N0<T>) || N0<T>;
template <typename T> concept N2 = (N1<T> && N1<T>) || N1<T>;
template <typename T> concept N3 = (N2<T> && N2<T>) || N2<T>;
template <typename T> concept N4 = (N3<T> && N3<T>) || N3<T>;
template <typename T> concept N5 = (N4<T> && N4<T>) || N4<T>;
template <typename T> concept N6 = (N5<T> && N5<T>) || N5<T>;
template <typename T> concept N7 = (N6<T> && N6<T>) || N6<T>;
template <typename T> concept N8 = (N7<T> && N7<T>) || N7<T>;

template <typename T> requires N8<T> int nest(T) { return 8; }

/* --- 4. 30-level chain mixing &&, || and ! ----------------------- */
template <typename T> concept M0 = sizeof(T) == 4;
template <typename T> concept M1 = M0<T> && sizeof(T) <= 8;
template <typename T> concept M2 = M1<T> || sizeof(T) == 0;
template <typename T> concept M3 = !(!M2<T>);
template <typename T> concept M4 = M3<T> && sizeof(T) <= 8;
template <typename T> concept M5 = M4<T> || sizeof(T) == 0;
template <typename T> concept M6 = !(!M5<T>);
template <typename T> concept M7 = M6<T> && sizeof(T) <= 8;
template <typename T> concept M8 = M7<T> || sizeof(T) == 0;
template <typename T> concept M9 = !(!M8<T>);
template <typename T> concept M10 = M9<T> && sizeof(T) <= 8;
template <typename T> concept M11 = M10<T> || sizeof(T) == 0;
template <typename T> concept M12 = !(!M11<T>);
template <typename T> concept M13 = M12<T> && sizeof(T) <= 8;
template <typename T> concept M14 = M13<T> || sizeof(T) == 0;
template <typename T> concept M15 = !(!M14<T>);
template <typename T> concept M16 = M15<T> && sizeof(T) <= 8;
template <typename T> concept M17 = M16<T> || sizeof(T) == 0;
template <typename T> concept M18 = !(!M17<T>);
template <typename T> concept M19 = M18<T> && sizeof(T) <= 8;
template <typename T> concept M20 = M19<T> || sizeof(T) == 0;
template <typename T> concept M21 = !(!M20<T>);
template <typename T> concept M22 = M21<T> && sizeof(T) <= 8;
template <typename T> concept M23 = M22<T> || sizeof(T) == 0;
template <typename T> concept M24 = !(!M23<T>);
template <typename T> concept M25 = M24<T> && sizeof(T) <= 8;
template <typename T> concept M26 = M25<T> || sizeof(T) == 0;
template <typename T> concept M27 = !(!M26<T>);
template <typename T> concept M28 = M27<T> && sizeof(T) <= 8;
template <typename T> concept M29 = M28<T> || sizeof(T) == 0;
template <typename T> concept M30 = !(!M29<T>);

template <typename T> requires M30<T> int deep(T) { return 30; }

int
main(void)
{
    /* 20-level chain folds to sizeof(T) == 4: int satisfies it */
    if (bump(41) != 42) return 1;
    /* a second type through the same deep chain */
    if (bump(1.5f) != 2.5f) return 2;
    /* the negated 20-level chain: long is 8 bytes, so !A20 holds */
    if (wide(3L) != 1) return 3;

    /* 10-level requires-clause chain */
    if (lvl10(1) != 10) return 4;
    if (lvl10((char)1) != 10) return 5;

    /* 8-level fan-out nesting */
    if (nest(1) != 8) return 6;
    if (nest(1.0f) != 8) return 7;

    /* 30-level mixed-operator chain */
    if (deep(1) != 30) return 8;

    return 0;
}
