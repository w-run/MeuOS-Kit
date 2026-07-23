/* C99 <stdint.h> exact-width integer types (§7.18) */
#include <stdint.h>
extern int puts(const char *);

int main(void) {
    /* Type sizes */
    if (sizeof(int64_t) != 8)  { puts("FAIL: sizeof int64_t"); return 1; }
    if (sizeof(uint64_t) != 8) { puts("FAIL: sizeof uint64_t"); return 1; }
    if (sizeof(int32_t) != 4)  { puts("FAIL: sizeof int32_t"); return 1; }
    if (sizeof(uint32_t) != 4) { puts("FAIL: sizeof uint32_t"); return 1; }
    if (sizeof(int16_t) != 2)  { puts("FAIL: sizeof int16_t"); return 1; }
    if (sizeof(uint16_t) != 2) { puts("FAIL: sizeof uint16_t"); return 1; }
    if (sizeof(int8_t) != 1)   { puts("FAIL: sizeof int8_t"); return 1; }
    if (sizeof(uint8_t) != 1)  { puts("FAIL: sizeof uint8_t"); return 1; }

    /* Values using direct literals */
    int64_t i64 = 42LL;
    uint64_t u64 = 100ULL;
    if (i64 != 42)   { puts("FAIL: int64_t val"); return 1; }
    if (u64 != 100)  { puts("FAIL: uint64_t val"); return 1; }

    /* Type limits */
    if (INT32_MAX != 2147483647)      { puts("FAIL: INT32_MAX"); return 1; }
    if (INT32_MIN != -2147483647-1)   { puts("FAIL: INT32_MIN"); return 1; }
    if (UINT32_MAX != 4294967295U)    { puts("FAIL: UINT32_MAX"); return 1; }

    /* INT64_MIN via long long literal */
    int64_t min = -9223372036854775807LL - 1;
    if (min != -9223372036854775807LL - 1) { puts("FAIL: INT64_MIN"); return 1; }

    puts("PASS");
    return 0;
}
