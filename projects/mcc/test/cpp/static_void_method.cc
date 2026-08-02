/* static_void_method.cc — static void method via Class::method() (m++).
 *
 * Regression: a static member function with `void` return type called as
 * `Class::method(args)` in a statement position was misparsed as an object
 * construction (`Class method(args)`) and rejected with "no matching
 * constructor".  The class-qualified call must lower to the static method
 * call `Class_method(args)` regardless of return type.
 */
extern int puts(const char *);

class Bank {
public:
    static int balance;
    static void deposit(int v) { balance = balance + v; }
    static int balance_of() { return balance; }
};
int Bank::balance = 100;

int main(void) {
    Bank::deposit(50);            /* statement position, void return */
    if (Bank::balance_of() != 150) return 1;
    Bank::deposit(10);
    if (Bank::balance_of() != 160) return 2;
    return 0;
}
