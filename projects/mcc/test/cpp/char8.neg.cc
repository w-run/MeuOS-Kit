/* Negative test: C++20 char8_t type / u8 prefix must be rejected.
 * check-cpp-neg compiles this expecting failure.
 */
int main(void) {
    char8_t c = u8'a';   /* char8_t: not supported */
    return (int)c;
}
