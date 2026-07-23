/* C99 long long type (§5.2.4.2.1, §6.7.2) */
extern int puts(const char *);

int main(void) {
    long long ll = 9223372036854775807LL;          /* LLONG_MAX */
    unsigned long long ull = 18446744073709551615ULL; /* ULLONG_MAX */

    /* Size */
    if (sizeof(long long) != 8)            { puts("FAIL: sizeof long long"); return 1; }
    if (sizeof(unsigned long long) != 8)   { puts("FAIL: sizeof unsigned long long"); return 1; }

    /* Values */
    if (ll != 9223372036854775807LL)       { puts("FAIL: LLONG_MAX"); return 1; }
    if (ull != 18446744073709551615ULL)    { puts("FAIL: ULLONG_MAX"); return 1; }

    /* Arithmetic */
    long long a = 1000000000000LL + 2000000000000LL;
    if (a != 3000000000000LL)              { puts("FAIL: long long add"); return 1; }

    long long b = 5000000000000LL - 3000000000000LL;
    if (b != 2000000000000LL)              { puts("FAIL: long long sub"); return 1; }

    long long c = 3000000LL * 4000000LL;
    if (c != 12000000000000LL)             { puts("FAIL: long long mul"); return 1; }

    /* Unsigned wrapping */
    unsigned long long ua = 0xFFFFFFFFFFFFFFFFULL;
    unsigned long long ub = ua + 1;
    if (ub != 0)                           { puts("FAIL: ull wrap"); return 1; }

    /* Comparison */
    if (!(9223372036854775806LL < 9223372036854775807LL)) {
        puts("FAIL: long long cmp");
        return 1;
    }

    puts("PASS");
    return 0;
}
