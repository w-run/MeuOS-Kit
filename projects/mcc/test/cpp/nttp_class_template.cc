/* nttp_class_template.cc — class templates with dependent-type NTTPs and
 * mixed explicit arguments.
 *
 * Extends nttp_dep_type.cc with the cases that distinguish *per
 * instantiation* state: the same template instantiated twice with
 * different NTTP values (`C<int,5>` and `C<int,7>`).  Member bodies of a
 * template instantiation are buffered and parsed lazily, long after the
 * instantiation itself returned — so each body must see the template
 * parameter bindings of *its own* instantiation, not those left behind by
 * whichever instantiation happened last.
 *
 * Returns 0 on success. */

/* dependent NTTP: N has the type of the first argument */
template<typename T, T N>
struct C {
    T val() { return N; }
};

/* plain (non-dependent) NTTP */
template<int N>
struct P {
    int val() { return N; }
};

/* mixed: type parameter, plain NTTP, dependent NTTP */
template<typename T, int M, T K>
struct D {
    T sum() { return M + K; }
};

/* dependent NTTP used as an array bound */
template<typename T, T N>
struct Buf {
    T data[N];
    int size() { return (int)N; }
};

/* function template with a dependent NTTP */
template<typename T, T N>
T add_n(T x) { return x + N; }

int main(void) {
    /* two instantiations of the same template must not share the binding */
    C<int, 5> a;
    C<int, 7> b;
    if (a.val() != 5) return 1;
    if (b.val() != 7) return 2;

    /* the same NTTP value under a different type is a distinct instance */
    C<long, 5> c;
    if (c.val() != 5) return 3;

    /* plain NTTP, likewise instantiated twice */
    P<3> p1;
    P<9> p2;
    if (p1.val() != 3) return 4;
    if (p2.val() != 9) return 5;

    /* mixed explicit arguments, twice */
    D<long, 2, 4> d1;
    D<long, 5, 6> d2;
    if (d1.sum() != 6) return 6;
    if (d2.sum() != 11) return 7;

    /* dependent NTTP as an array bound */
    Buf<int, 4> buf;
    for (int i = 0; i < 4; ++i)
        buf.data[i] = i * 2;
    if (buf.size() != 4) return 8;
    if (buf.data[3] != 6) return 9;
    if (sizeof(buf.data) != 4 * sizeof(int)) return 10;

    /* function templates with explicit type + value arguments */
    if (add_n<int, 5>(3) != 8) return 11;
    if (add_n<int, 10>(3) != 13) return 12;
    if (add_n<long, 100>(5) != 105) return 13;

    return 0;
}
