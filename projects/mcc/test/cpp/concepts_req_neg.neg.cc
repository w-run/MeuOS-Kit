/* Negative test: a conjunction that is false for the deduced type must
 * be rejected by the requires-clause (`requires Small<T> && !Small<T>`
 * is never satisfiable, so the constrained call errors).
 */
template <typename T> concept Small = sizeof(T) <= 4;

template <typename T> requires Small<T> && !Small<T> int f(T x) { return x + 1; }

int main(void) {
    return f(41);
}
