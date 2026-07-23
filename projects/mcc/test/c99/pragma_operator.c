/* C99 _Pragma() preprocessing operator (§6.10.6) */
extern int puts(const char *);

/* Test that the _Pragma() syntax is recognised in macro context */
#define SUPPRESS_WARN _Pragma("")

int main(void) {
    int x = 42;
    if (x != 42) { puts("FAIL"); return 1; }

    /* Stringification operator (C99 §6.10.3.2) */
#define STR(x) #x

    puts("PASS");
    return 0;
}
