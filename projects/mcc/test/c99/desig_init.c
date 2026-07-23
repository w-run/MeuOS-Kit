/* C99 designated initializers (§6.7.8) */
extern int puts(const char *);

struct pair { int first, second; };

int main(void) {
    /* .field = value */
    struct pair p = {.second = 2, .first = 1};
    if (p.first != 1 || p.second != 2) {
        puts("FAIL: .field init");
        return 1;
    }

    /* [index] = value */
    int arr[5] = {[2] = 99, [0] = 11};
    if (arr[0] != 11 || arr[1] != 0 || arr[2] != 99) {
        puts("FAIL: [index] init");
        return 1;
    }

    puts("PASS");
    return 0;
}
