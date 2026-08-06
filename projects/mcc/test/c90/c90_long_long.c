/* C90 long long 扩展（非标准，但广泛支持） */

extern int puts(const char *);

int main(void)
{
    long long ll = 9223372036854775807LL;
    unsigned long long ull = 18446744073709551615ULL;

    /* sizeof */
    if (sizeof(long long) != 8)            { puts("FAIL: sizeof long long"); return 1; }
    if (sizeof(unsigned long long) != 8)   { puts("FAIL: sizeof unsigned long long"); return 1; }

    /* 值验证 */
    if (ll != 9223372036854775807LL)       { puts("FAIL: LLONG_MAX"); return 1; }
    if (ull != 18446744073709551615ULL)    { puts("FAIL: ULLONG_MAX"); return 1; }

    /* 算术 */
    long long a = 1000000000000LL + 2000000000000LL;
    if (a != 3000000000000LL)              { puts("FAIL: long long add"); return 1; }

    long long b = 5000000000000LL - 3000000000000LL;
    if (b != 2000000000000LL)              { puts("FAIL: long long sub"); return 1; }

    /* 无符号溢出 */
    unsigned long long ua = 0xFFFFFFFFFFFFFFFFULL;
    unsigned long long ub = ua + 1;
    if (ub != 0)                           { puts("FAIL: ull wrap"); return 1; }

    puts("PASS");
    return 0;
}