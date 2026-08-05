/* Positive test: C++20 constinit specifier.
 * check-cpp-func compiles and runs this expecting exit status 0.
 *
 * constinit demands constant initialization but, unlike constexpr, does
 * not make the object const -- it stays mutable.
 */
constinit int x = 7;

constinit int y = 3 * 4 + 1;   /* constant-folded initializer */

int main(void) {
    if (x != 7)
        return 1;
    if (y != 13)
        return 2;
    x = 9;                     /* mutable: constinit is not const */
    if (x != 9)
        return 3;
    return 0;
}
