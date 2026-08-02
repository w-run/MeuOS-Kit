/* concepts_unsat.neg.cc — a requires-clause whose concept is FALSE for
 * the deduced type must reject the instantiation.
 *
 * Distinct from concepts_req_neg.neg.cc (which pins a never-satisfiable
 * conjunction `Small<T> && !Small<T>`): here the concept is perfectly
 * satisfiable in general — a 2-byte type would pass — but the type
 * actually deduced at the call site (`int`, 4 bytes) does not satisfy
 * it.  This pins that satisfaction is evaluated against the DEDUCED
 * type, not just for syntactic contradiction.
 *
 * Expected diagnostic: "template 'f' instantiated with a type that does
 * not satisfy its requires-clause".
 */
template <typename T> concept TwoByte = sizeof(T) == 2;

template <typename T> requires TwoByte<T> int f(T x) { return (int)x; }

int
main(void)
{
    return f(41);   /* int is 4 bytes -> requires-clause unsatisfied */
}
