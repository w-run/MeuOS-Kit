/* Negative test: assigning a string literal to an int is a type error.
 * check-cpp-neg compiles this expecting failure.
 */
int main(void) {
    int x;
    x = "hello";          /* int cannot be assigned a const char* */
    return x;
}
