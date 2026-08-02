/* large_array_init.c — C99 large-array initialization.
 *
 * Covers:
 *  - a large fully-initialized array (1024 ints), verified by summation
 *  - partial initialization: unspecified elements are zero-initialized
 *  - large 2-D array with per-row initializers
 *  - char array initialized from a string literal
 *  - designated overrides scattered across a large initializer
 */
extern int puts(const char *);

int main(void) {
    /* large fully-initialized array: sum of 0..1023 = 523776 */
    {
        int a[1024];
        int i;
        long sum = 0;
        for (i = 0; i < 1024; i = i + 1)
            a[i] = i;
        for (i = 0; i < 1024; i = i + 1)
            sum = sum + a[i];
        if (sum != 523776L) { puts("FAIL: full 1024 sum"); return 1; }
    }

    /* partial initialization: remaining elements are zero */
    {
        int a[8] = { 1, 2, 3 };
        if (a[0] != 1 || a[2] != 3)  { puts("FAIL: partial head"); return 2; }
        if (a[3] != 0 || a[7] != 0)  { puts("FAIL: partial tail zero"); return 3; }
    }

    /* large partial init: only first few set, rest zero */
    {
        int a[256] = { 5, 6, 7 };
        if (a[0] != 5 || a[2] != 7)  { puts("FAIL: 256 head"); return 4; }
        if (a[3] != 0 || a[255] != 0){ puts("FAIL: 256 tail zero"); return 5; }
    }

    /* large 2-D array: row-major fill then read back */
    {
        int m[32][16];
        int i, j, sum = 0;
        for (i = 0; i < 32; i = i + 1)
            for (j = 0; j < 16; j = j + 1)
                m[i][j] = i * 16 + j;
        for (i = 0; i < 32; i = i + 1)
            for (j = 0; j < 16; j = j + 1)
                sum = sum + m[i][j];
        /* sum 0..511 = 130816 */
        if (sum != 130816) { puts("FAIL: 2D sum"); return 6; }
        if (m[31][15] != 511) { puts("FAIL: 2D last"); return 7; }
    }

    /* 2-D partial init: only first row provided, rest zero */
    {
        int m[8][8] = { { 1, 2 } };
        if (m[0][0] != 1 || m[0][1] != 2) { puts("FAIL: 2D part head"); return 8; }
        if (m[0][2] != 0 || m[7][7] != 0) { puts("FAIL: 2D part zero"); return 9; }
    }

    /* char array from a string literal */
    {
        char s[12] = "hello";
        if (s[0] != 'h' || s[4] != 'o') { puts("FAIL: str head"); return 10; }
        if (s[5] != '\0')               { puts("FAIL: str nul"); return 11; }
        if (s[6] != 0 || s[11] != 0)    { puts("FAIL: str tail zero"); return 12; }
    }

    /* designated overrides in a large initializer */
    {
        int a[64] = { [0] = 7, [63] = 9, [31] = 5 };
        if (a[0] != 7 || a[31] != 5 || a[63] != 9) { puts("FAIL: desig"); return 13; }
        if (a[1] != 0 || a[62] != 0) { puts("FAIL: desig zero"); return 14; }
    }

    puts("PASS");
    return 0;
}
