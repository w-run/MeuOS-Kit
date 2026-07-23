/* C99 extern linkage (§6.7.4) */
extern int puts(const char *);
extern int ext1;
extern int *ext2;
extern int ext3;
extern int ext_fn1(int);
extern int ext_fn2(int);

int main(void) {
    /* External variables */
    if (ext1 != 5) { puts("FAIL: ext1"); return 1; }
    if (*ext2 != 5) { puts("FAIL: *ext2"); return 1; }
    if (ext3 != 7) { puts("FAIL: ext3"); return 1; }

    /* External functions */
    if (ext_fn1(5) != 5) { puts("FAIL: ext_fn1"); return 1; }
    if (ext_fn2(8) != 8) { puts("FAIL: ext_fn2"); return 1; }

    puts("PASS");
    return 0;
}
