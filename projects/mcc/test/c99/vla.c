/* C99 variable-length arrays (§6.7.5.2) */
extern int puts(const char *);

int main(void) {
    int i, total = 0, n = 5;
    int values[n];
    for (i = 0; i < n; ++i) {
        values[i] = i;
        total += values[i];
    }
    if (total != 10) {
        puts("FAIL");
        return 1;
    }

    /* sizeof VLA */
    if (sizeof(values) != 5 * sizeof(int)) {
        puts("FAIL: sizeof VLA");
        return 1;
    }

    puts("PASS");
    return 0;
}
