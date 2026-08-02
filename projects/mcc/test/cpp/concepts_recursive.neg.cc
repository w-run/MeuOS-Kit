/* concepts_recursive.neg.cc — a concept reference chain deeper than
 * MAX_CONSTRAINT_DEPTH (16) must be rejected.  C17 references C16, ...
 * C1 references C0; expanding C17 exceeds the depth guard, the innermost
 * reference is left unexpanded and fails to parse — a compile error.
 */
template <typename T> concept C0 = sizeof(T) == 4;
template <typename T> concept C1 = C0<T>;
template <typename T> concept C2 = C1<T>;
template <typename T> concept C3 = C2<T>;
template <typename T> concept C4 = C3<T>;
template <typename T> concept C5 = C4<T>;
template <typename T> concept C6 = C5<T>;
template <typename T> concept C7 = C6<T>;
template <typename T> concept C8 = C7<T>;
template <typename T> concept C9 = C8<T>;
template <typename T> concept C10 = C9<T>;
template <typename T> concept C11 = C10<T>;
template <typename T> concept C12 = C11<T>;
template <typename T> concept C13 = C12<T>;
template <typename T> concept C14 = C13<T>;
template <typename T> concept C15 = C14<T>;
template <typename T> concept C16 = C15<T>;
template <typename T> concept C17 = C16<T>;
template <typename T> requires C17<T> T next(T x) { return x + 1; }
int main(void) { return next(41); }
