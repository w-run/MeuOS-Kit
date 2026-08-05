/* Negative test: accessing a member defined by more than one base
 * subobject must be rejected as ambiguous (multiple inheritance).
 * check-cpp-neg compiles this expecting failure.
 */
class A { public: int x; };
class B { public: int x; };
class D : public A, public B { };

int main(void) {
    D d;
    return d.x;   /* ambiguous: A::x vs B::x */
}
