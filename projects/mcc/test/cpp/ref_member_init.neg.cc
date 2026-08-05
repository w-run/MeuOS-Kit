/* Negative test: a reference data member (`int &ref`) initialized in the
 * initializer list must be rejected — m++ finds no matching constructor
 * for a class with a reference member.
 * check-cpp-neg compiles this expecting failure.
 */
class R {
public:
    int &ref;
    int extra;
    R(int &x, int e) : ref(x), extra(e) { }
};

int main(void) {
    int v = 5;
    R r(v, 9);
    return r.ref == 5 ? 0 : 1;
}
