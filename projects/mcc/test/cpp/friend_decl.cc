/* C++11 friend declarations: an in-class friend function definition
 * (with private-member access), a friend operator overload, and a friend
 * class.  Each check returns a distinct exit code; exit 0 = all passed.
 */
class A {
    int n;
public:
    A(int v) : n(v) {}
    /* in-class friend function definition: a file-scope free function
     * granted access to A's private member n */
    friend int get_n(const A &a) { return a.n; }
    /* friend operator overload: same access, through operator syntax */
    friend int operator==(const A &a, const A &b) { return a.n == b.n; }
    /* friend class: B's methods may access A's private members */
    friend class B;
};

class B {
public:
    int steal(const A &a) { return a.n; }
};

int main(void) {
    A a(42);
    if (get_n(a) != 42) return 1;      /* friend free function */
    if (!(a == A(42))) return 2;       /* friend operator== */
    if (a == A(43)) return 3;
    B b;
    if (b.steal(a) != 42) return 4;    /* friend class */
    return 0;
}
