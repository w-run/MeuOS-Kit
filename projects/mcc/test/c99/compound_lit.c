/* C99 compound literals (§6.5.2.5) */
extern int puts(const char *);

typedef struct { int x, y; } pair;

int main(void) {
    int *items = (int[]){1, 2, 3};
    if (items[2] != 3) {
        puts("FAIL: compound literal");
        return 1;
    }

    /* Named struct type compound literal */
    pair *p = &(pair){10, 20};
    if (p->x != 10 || p->y != 20) {
        puts("FAIL: struct compound literal");
        return 1;
    }

    puts("PASS");
    return 0;
}
