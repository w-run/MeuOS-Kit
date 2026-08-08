/* test_fusion.c — 融合引擎回归测试 */
#include "mz.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;
#define TEST(name, expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL: %s\n", name); \
        failures++; \
    } else { \
        printf("PASS: %s\n", name); \
    } \
} while(0)

static void test_roundtrip(const char *name, const uint8_t *data, size_t len)
{
    void *c = NULL; size_t cl = 0;
    int r = mz_fusion_compress(data, len, &c, &cl, 6);
    TEST(name, r > 0);
    if (r <= 0) return;

    void *d = NULL; size_t dl = 0;
    r = mz_fusion_decompress(c, cl, &d, &dl);
    TEST(name, r > 0);
    TEST(name, dl == len);
    if (r > 0 && dl == len) {
        TEST(name, memcmp(data, d, len) == 0);
    }
    printf("  %s: %zu -> %zu (%.1f%%)\n", name, len, cl, 100.0*cl/len);
    free(c); free(d);
}

int main(void)
{
    /* 1. 文本 */
    const char *text = "Hello, MeuOS Fusion Engine! This is a test. "
                       "The quick brown fox jumps over the lazy dog. "
                       "Repeated data repeated data repeated data repeated data. "
                       "AAAAAABBBBBBCCCCCCDDDDDDEEEEEEFFFFF";
    test_roundtrip("text", (const uint8_t*)text, strlen(text)+1);

    /* 2. 周期文本 (大) */
    {
        size_t sz = 50000;
        uint8_t *d = malloc(sz);
        for (size_t i = 0; i < sz; i++)
            d[i] = "abcdefghij"[i % 10];
        test_roundtrip("periodic-50k", d, sz);
        free(d);
    }

    /* 3. 随机数据 */
    {
        size_t sz = 50000;
        uint8_t *d = malloc(sz);
        srand(42);
        for (size_t i = 0; i < sz; i++)
            d[i] = (uint8_t)(rand() & 0xFF);
        test_roundtrip("random-50k", d, sz);
        free(d);
    }

    /* 4. level 1-9 全覆盖 */
    {
        size_t sz = 10000;
        uint8_t *d = malloc(sz);
        for (size_t i = 0; i < sz; i++)
            d[i] = (uint8_t)(i * 73 + (i >> 3) * 13);
        for (int lv = 1; lv <= 9; lv++) {
            void *c = NULL; size_t cl = 0;
            int r = mz_fusion_compress(d, sz, &c, &cl, lv);
            TEST("level compress ok", r > 0);
            if (r > 0) {
                void *dd = NULL; size_t dl = 0;
                r = mz_fusion_decompress(c, cl, &dd, &dl);
                TEST("level roundtrip", r > 0 && dl == sz && memcmp(d, dd, sz) == 0);
                free(c); free(dd);
            }
        }
        free(d);
    }

    /* 5. 小文件 RAW */
    {
        const char *tiny = "hi";
        test_roundtrip("tiny", (const uint8_t*)tiny, 3);
    }

    /* 6. 空输入 */
    {
        void *c = NULL; size_t cl = 0;
        int r = mz_fusion_compress("", 0, &c, &cl, 6);
        TEST("empty compress", r <= 0);
    }

    printf("\n%d test(s) FAILED\n", failures);
    return failures ? 1 : 0;
}
