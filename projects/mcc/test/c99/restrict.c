/* C99 restrict pointer qualifier (§6.7.3) */
extern int puts(const char *);

static void
assign(int *restrict a, int *restrict b, int n)
{
    int i;
    for (i = 0; i < n; i++)
        a[i] = b[i] + 1;
}

/* restrict in function parameter with array syntax */
void funcy_type(int arg[restrict static 3]) {}

int main(void) {
    int src[4] = {1, 2, 3, 4};
    int dst[4];

    assign(dst, src, 4);

    if (dst[0] != 2 || dst[1] != 3 || dst[2] != 4 || dst[3] != 5) {
        puts("FAIL: restrict");
        return 1;
    }

    /* volatile qualifier */
    { volatile int x; }
    { int volatile x; }
    { volatile int x; }
    { int volatile *volatile x; }

    puts("PASS");
    return 0;
}
