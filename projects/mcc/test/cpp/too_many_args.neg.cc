/* Negative test: calling a member function with too many arguments must
 * be rejected (no matching signature).  check-cpp-neg expects failure.
 */
class Calc {
public:
    int f(int x) { return x + 1; }
};

int main(void) {
    Calc c;
    return c.f(1, 2);  /* f takes one argument */
}
