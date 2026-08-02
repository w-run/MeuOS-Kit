/* Negative test: the address of an overloaded member function is
 * ambiguous and must be rejected.  check-cpp-neg compiles this expecting
 * failure.
 */
class Calc {
public:
    int add(int a) { return a + 1; }
    int add(int a, int b) { return a + b; }
};

int main(void) {
    Calc c;
    void *p = &c.add;   /* ambiguous: two overloads */
    (void)p;
    return 0;
}
