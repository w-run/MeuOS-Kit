/* Negative test: two declarations of the same function with conflicting
 * return types (int vs char) is ill-formed. Distinct from
 * redefinition_func.neg.cc (two definitions) — this pins a conflicting
 * redeclaration. check-cpp-neg expects compilation failure.
 */
int g(void);
char g(void);
int main(void) { return g(); }
