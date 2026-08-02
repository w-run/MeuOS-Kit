/* static_member.cc — static data members & static methods (C.2.3, m++).
 *
 * Covers:
 *  - static data members declared in-class, defined at file scope with
 *    an initializer, and read/written via `Class::member`
 *  - static data shared across all instances (instance-independent)
 *  - static methods: called via `Class::method()`, mutating static state,
 *    calling other static methods
 *  - multiple classes with independent static storage
 *
 * NOTE: static members are not reachable through derived-class objects in
 * m++ yet (inheritance of statics is not implemented); this test only uses
 * the declaring class.
 *
 * Each check returns a distinct exit code; run via `check-cpp-func`.
 */

class Counter {
public:
    static int total;
    static int next() { total = total + 1; return total; }
    static int peek() { return total; }
};
int Counter::total = 0;

class Bank {
public:
    static int balance;
    static int deposit(int v) { balance = balance + v; return balance; }
    static int balance_of() { return balance; }
};
int Bank::balance = 100;

class Tags {
public:
    static int a;
    static int b;
    static int sum() { return a + b; }
    static int twice(int v) { return Tags::sum() + v; }
};
int Tags::a = 10;
int Tags::b = 32;

class Mixed {
public:
    int inst;
    static int shared;
    Mixed() { inst = 0; shared = shared + 1; }
    int get() { return inst; }
};
int Mixed::shared = 0;

int main(void) {
    /* static data shared across instances */
    Mixed m1;
    Mixed m2;
    Mixed m3;
    if (Mixed::shared != 3) return 1;

    /* static methods mutating static state */
    if (Counter::next() != 1) return 2;
    if (Counter::next() != 2) return 3;
    if (Counter::peek() != 2) return 4;
    if (Counter::total != 2) return 5;

    /* independent statics in another class */
    if (Bank::deposit(50) != 150) return 6;
    if (Bank::balance_of() != 150) return 7;

    /* static method calling another static method (class-qualified) */
    if (Tags::sum() != 42) return 8;
    if (Tags::twice(8) != 50) return 9;

    /* instance members still work alongside statics */
    if (m1.get() != 0) return 10;
    m1.inst = 7;
    if (m1.get() != 7) return 11;

    return 0;
}
