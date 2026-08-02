/* Negative test: a conjunction of two named concepts in a
 * requires-clause (`requires Small<T> && NotVoid<T>`) must be rejected —
 * m++ does not support `&&` between concept names in a requires-clause
 * (the constrained function is never declared, so the call errors).
 * check-cpp-neg compiles this expecting failure.
 */
template <typename T> concept Small = sizeof(T) <= 4;
template <typename T> concept NotVoid = sizeof(T) == 4;

template <typename T> requires Small<T> && NotVoid<T> T f(T x) { return x + 1; }

int main(void) {
    return f(41) == 42 ? 0 : 1;
}
