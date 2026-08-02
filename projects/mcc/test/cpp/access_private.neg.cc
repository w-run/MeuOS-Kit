/* Negative test: accessing a private member must be rejected (C.2.3
 * access control).  check-cpp-neg compiles this expecting failure.
 */
class Secret {
private:
    int hidden;
public:
    Secret() { hidden = 42; }
    int get() { return hidden; }
};

int main(void) {
    Secret s;
    return s.hidden;   /* private: not accessible */
}
