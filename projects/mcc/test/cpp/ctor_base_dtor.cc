/* ctor_base_dtor.cc — inherited destructor chain (m++ end-to-end).
 *
 * Regression: a derived-class destructor must run each base's destructor
 * after its own body (reverse construction order).  Output:
 *   B+ D+ done D- B-
 * exit 0.
 */
extern int printf(const char *, ...);

class Base {
public:
    Base()  { printf("B+\n"); }
    ~Base() { printf("B-\n"); }
};
class Der : public Base {
public:
    Der()  { printf("D+\n"); }
    ~Der() { printf("D-\n"); }
};

int main(void) {
    Der d;
    printf("done\n");
    return 0;
}
