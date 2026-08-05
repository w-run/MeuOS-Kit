/* nttp_dep_type.cc — C++20 dependent-type non-type template parameters.
 *
 * `template<typename T, T N>`: the NTTP's type names an earlier type
 * parameter.  The type binds to the *argument's* concrete type at
 * instantiation (C<int, 7> → N:int; C<long, 7> → N:long).
 *
 * Returns 0 on success. */
template<typename T, T N>
T add_n(T x) { return x + N; }

template<typename T, T N>
struct C {
    T val() { return N; }
};

/* dependent NTTP with a class template + mixed explicit args */
template<typename T, int M, T K>
struct D {
    T sum() { return M + K; }
};

int main(void) {
    if (add_n<int, 5>(3) != 8) return 1;
    if (add_n<long, 10>(5) != 15) return 2;

    C<int, 7> c;
    if (c.val() != 7) return 3;

    D<long, 2, 4> d;
    if (d.sum() != 6) return 4;

    return 0;
}
