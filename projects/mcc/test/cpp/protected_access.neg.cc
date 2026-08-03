/* Negative test: accessing a protected member from outside the class
 * hierarchy is ill-formed (C.2.3 access control). Distinct from
 * access_private.neg.cc (private) — this pins protected.
 * check-cpp-neg expects compilation failure.
 */
class Base {
protected:
    int p;
};
int main(void) {
    Base b;
    return b.p;           /* protected: not accessible from main */
}
