/* C++20 char8_t type -- basic sanity.
 * char8_t is a distinct unsigned 8-bit character type.
 * u8 prefix produces char8_t character / const char8_t* string.
 * (Currently u8 literals are typed as char -- the type is recognized
 *  but the literal type assignment is a separate gap.) */
int main(void) {
    char8_t c = 120;    /* char8_t is unsigned 8-bit */
    char8_t d = 'x';    /* char literal fits in char8_t */
    char8_t arr[] = { 1, 2, 3 };
    char8_t *p = arr;
    int sum = c + d + p[0] + p[1] + p[2];
    return sum == 120 + 120 + 1 + 2 + 3 ? 0 : 1;
}