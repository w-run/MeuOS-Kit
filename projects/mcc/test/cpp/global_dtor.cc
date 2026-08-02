/* Global object construction and destruction (C.2.3): `Logger g(7)` is
 * constructed by __mxx_global_var_init (via .init_array) before main and
 * destroyed in reverse order by __mxx_global_var_fini (via .fini_array)
 * after main.  Each dtor prints its id; expected order after main:
 * dtor 3, dtor 2, dtor 1.
 */
extern int puts(const char *);
extern int printf(const char *, ...);

class Logger {
public:
    Logger(int v) { id = v; }
    ~Logger() { printf("dtor %d\n", id); }
    int id;
};

Logger a(1);
Logger b(2);
Logger c(3);

int main(void) {
    puts("main");
    return 0;
}
