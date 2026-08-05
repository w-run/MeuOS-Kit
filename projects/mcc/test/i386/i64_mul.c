/* i386 i64 multiply: must compose from three 32x32->64 `mull`s (cross terms
 * a.lo*b.hi and a.hi*b.lo), NOT two independent 32-bit `imull`s of the halves
 * (which drops the cross terms and corrupts the high half).  Regression source
 * for the i64 MUL path in i386_memit.c. */
long long m1(long long a, long long b) { return a * b; }
unsigned long long m2(unsigned long long a, unsigned long long b) { return a * b; }

int main(void) {
    if (m1(0x123456789LL, 0x112233445LL) != (0x123456789LL * 0x112233445LL)) return 1;
    if (m2(0xFFFFFFFF00000000ULL, 0x00000000FFFFFFFFULL)
        != (0xFFFFFFFF00000000ULL * 0x00000000FFFFFFFFULL)) return 2;
    if (m1(-123456789LL, 987654321LL) != (-123456789LL * 987654321LL)) return 3;
    return 0;
}
