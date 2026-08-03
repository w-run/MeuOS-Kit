/* nttp_constexpr_fold.cc — dependent-type NTTP values in
 * constant-expression contexts.
 *
 * An NTTP is a compile-time constant, so it must fold inside
 * static_assert, array bounds and enumerator initialisers — including
 * when its type is a dependent one (`template<typename T, T N>`) and the
 * concrete type only becomes known at instantiation.
 *
 * This file is primarily a *coverage* test for NTTP constant folding, but
 * the `Val` class template near the bottom also acts as a *regression*
 * probe for T06: its member body is parsed lazily (after the
 * instantiation has returned), and two instantiations of the same
 * template (`Val<int,5>` then `Val<int,7>`) would otherwise clobber the
 * file-scope binding so `a.val()` returns the wrong value.  If that bug
 * ever regresses, this test fails.
 *
 * Returns 0 on success. */

/* constexpr function templates parameterised by a dependent NTTP */
template<typename T, T N>
constexpr T value_of() { return N; }

template<typename T, T N>
constexpr T twice() { return N * 2; }

template<typename T, T N>
constexpr T plus(T x) { return x + N; }

/* plain NTTP, for contrast */
template<int N>
constexpr int square() { return N * N; }

/* dependent NTTP as an array bound inside a class template */
template<typename T, T N>
struct Buf {
    T data[N];
    int cap() { return (int)N; }
};

/* Regression probe for T06 (lazy method-body binding).  The member body
 * is buffered at instantiation and parsed only when val() is called, by
 * which point a second instantiation would have overwritten the `N`
 * binding.  Two instances with distinct NTTPs must read distinct values. */
template<typename T, T N>
struct Val {
    T val() { return N; }
};

int main(void) {
    /* fold a dependent NTTP in static_assert */
    static_assert(value_of<int, 5>() == 5, "int NTTP folds");
    static_assert(value_of<long, 7>() == 7, "long NTTP folds");
    static_assert(value_of<char, 3>() == 3, "char NTTP folds");

    /* arithmetic on the folded value */
    static_assert(twice<int, 5>() == 10, "int NTTP arithmetic folds");
    static_assert(twice<long, 21>() == 42, "long NTTP arithmetic folds");
    static_assert(plus<int, 4>(6) == 10, "NTTP + argument folds");

    /* distinct instantiations fold to distinct values */
    static_assert(value_of<int, 1>() == 1, "instance 1");
    static_assert(value_of<int, 2>() == 2, "instance 2");
    static_assert(value_of<int, 1>() != value_of<int, 2>(), "distinct");

    /* plain NTTP folds too */
    static_assert(square<4>() == 16, "plain NTTP folds");
    static_assert(square<7>() == 49, "plain NTTP folds again");

    /* a folded NTTP is usable as an array bound */
    int arr[value_of<int, 6>()];
    if (sizeof(arr) != 6 * sizeof(int)) return 1;

    /* and as a class-template array bound */
    Buf<long, 3> b;
    b.data[0] = 10;
    b.data[2] = 30;
    if (sizeof(b.data) != 3 * sizeof(long)) return 2;
    if (b.data[0] + b.data[2] != 40) return 3;
    if (b.cap() != 3) return 4;

    /* runtime use of the same values agrees with the folded ones */
    if (value_of<int, 5>() != 5) return 5;
    if (twice<int, 5>() != 10) return 6;

    /* T06 regression probe: each instantiation's val() must see its own
     * NTTP binding, not the last one written to file scope. */
    Val<int, 5> v1;
    Val<int, 7> v2;
    if (v1.val() != 5) return 7;
    if (v2.val() != 7) return 8;

    return 0;
}
