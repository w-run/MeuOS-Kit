/* test_compress.c — regression test for mz */
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

int main(void)
{
    /* Test 1: Compress/decompress a small buffer */
    const char *text = "Hello, MeuOS Compression! This is a test of the built-in codec. "
                       "The quick brown fox jumps over the lazy dog. "
                       "Repeated data repeated data repeated data repeated data. "
                       "AAAAAABBBBBBCCCCCCDDDDDDEEEEEEFFFFF";
    size_t text_len = strlen(text) + 1;
    
    void *compressed;
    size_t comp_len;
    int ret = mz_compress(text, text_len, &compressed, &comp_len,
                          MZ_CODEC_LZ77, 1);
    TEST("compress succeeded", ret > 0);
    
    void *decompressed;
    size_t decomp_len;
    ret = mz_decompress(compressed, comp_len, &decompressed, &decomp_len,
                        MZ_CODEC_LZ77);
    TEST("decompress succeeded", ret > 0);
    TEST("decompressed size matches", decomp_len == text_len);
    TEST("decompressed content matches",
         memcmp(text, decompressed, text_len) == 0);
    
    printf("  original: %zu bytes -> compressed: %zu bytes (%.1f%%)\n",
           text_len, comp_len, 100.0 * comp_len / text_len);
    
    free(compressed);
    free(decompressed);
    
    /* Test 2: Empty input */
    ret = mz_compress("", 0, &compressed, &comp_len, MZ_CODEC_LZ77, 1);
    TEST("empty compress returns 0 or error", ret <= 0);
    
    /* Test 3: Large buffer — compress with levels 1-9 */
    {
        size_t large_sz = 10000;
        uint8_t *large = malloc(large_sz);
        for (size_t i = 0; i < large_sz; i++)
            large[i] = (uint8_t)(i * 73 + (i >> 3) * 13);
        
        for (int lv = 1; lv <= 9; lv++) {
            void *c; size_t cl;
            int r = mz_compress(large, large_sz, &c, &cl,
                                MZ_CODEC_LZ77, lv);
            TEST("compress level >0", r > 0);
            
            void *d; size_t dl;
            r = mz_decompress(c, cl, &d, &dl, MZ_CODEC_LZ77);
            TEST("decompress level >0", r > 0);
            TEST("decompress size matches", dl == large_sz);
            TEST("decompress content matches",
                 memcmp(large, d, large_sz) == 0);
            
            printf("  level %d: %zu -> %zu (%.1f%%)\n",
                   lv, large_sz, cl, 100.0 * cl / large_sz);
            free(c); free(d);
        }
        free(large);
    }
    
    
    /* Test 4: Solid compression (SOLID_START + SOLID_NEXT) */
    {
        struct mz_params params = { .level = 6, .flags = MZ_FLAG_SOLID };
        void *out_buf = NULL; size_t out_len = 0;

        int r = mz2_create(&out_buf, &out_len, &params);
        TEST("solid create", r == MZ_OK);

        if (r == MZ_OK) {
            const char *f1 = "hello world";
            const char *f2 = "hello meuos";
            r = mz2_add_file(out_buf, "a.txt", f1, strlen(f1), 0644);
            TEST("solid add a", r == MZ_OK);
            r = mz2_add_file(out_buf, "b.txt", f2, strlen(f2), 0644);
            TEST("solid add b", r == MZ_OK);

            void *result; size_t result_len;
            r = mz2_finish(out_buf, &result, &result_len);
            TEST("solid finish", r == MZ_OK);

            if (r == MZ_OK) {
                void *ctx;
                r = mz2_open(result, result_len, &ctx);
                TEST("solid open", r == MZ_OK);

                void *d1; size_t d1_len;
                r = mz2_read_file(ctx, "a.txt", &d1, &d1_len);
                TEST("solid read a", r == MZ_OK && d1_len == strlen(f1)
                     && memcmp(d1, f1, d1_len) == 0);
                free(d1);

                void *d2; size_t d2_len;
                r = mz2_read_file(ctx, "b.txt", &d2, &d2_len);
                TEST("solid read b", r == MZ_OK && d2_len == strlen(f2)
                     && memcmp(d2, f2, d2_len) == 0);
                free(d2);

                mz2_close(ctx);
                free(result);
            }
            free(out_buf);
        }
    }

    /* Test 5: Benchmark — level 1/3/5/7/9 on 100KB random and periodic data */
    {
        size_t sz = 100000;
        uint8_t *data = malloc(sz);
        srand(42);
        for (size_t i = 0; i < sz; i++)
            data[i] = (uint8_t)(rand() & 0xFF);

        printf("=== benchmark (100KB random data) ===\n");
        for (int lv = 1; lv <= 9; lv += 2) {
            void *c; size_t cl;
            int r = mz_compress(data, sz, &c, &cl, MZ_CODEC_MEUOS, lv);
            if (r > 0) {
                printf("  level %d: %zu -> %zu (%.1f%%)\n",
                       lv, sz, cl, 100.0 * cl / sz);
                free(c);
            }
        }
        free(data);

        char *text = malloc(sz);
        for (size_t i = 0; i < sz; i++)
            text[i] = "abcdefghij"[i % 10];
        printf("=== benchmark (100KB periodic text) ===\n");
        for (int lv = 1; lv <= 9; lv += 2) {
            void *c; size_t cl;
            int r = mz_compress(text, sz, &c, &cl, MZ_CODEC_MEUOS, lv);
            if (r > 0) {
                printf("  level %d: %zu -> %zu (%.1f%%)\n",
                       lv, sz, cl, 100.0 * cl / sz);
                free(c);
            }
        }
        free(text);
    }

    /* Test 5b: Level differentiation on 50KB periodic text
     *
     * Verifies that the level parameter is actually wired through to
     * the LZ77 search (chain depth, max match length, search window,
     * lazy matching). For highly-periodic input the per-match cost
     * dominates, so higher levels must consistently produce smaller
     * (or equal) output. We assert a strict monotone-decrease between
     * level 1 and level 9 and that every level produces a non-empty,
     * finite, in-range size. */
    {
        const size_t sz = 50000;
        const char *alphabet =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUV";
        const size_t period = 50;          /* strlen(alphabet) */
        char *text = (char *)malloc(sz);
        for (size_t i = 0; i < sz; i++)
            text[i] = alphabet[i % period];

        size_t sizes[10] = {0};
        int    rc[10]    = {0};
        printf("=== Level differentiation (50KB periodic, period=50) ===\n");
        for (int lv = 1; lv <= 9; lv++) {
            void *c = NULL; size_t cl = 0;
            rc[lv] = mz_compress(text, sz, &c, &cl, MZ_CODEC_MEUOS, lv);
            if (rc[lv] > 0) sizes[lv] = cl;
            printf("  level %d: %zu -> %zu (%.1f%%)\n",
                   lv, sz, cl, 100.0 * (double)cl / (double)sz);

            /* every level must round-trip */
            if (rc[lv] > 0) {
                void *d = NULL; size_t dl = 0;
                int dr = mz_decompress(c, cl, &d, &dl, MZ_CODEC_MEUOS);
                TEST("level diff: compress ok",   dr > 0);
                TEST("level diff: round-trip len", dl == sz);
                TEST("level diff: round-trip data",
                     memcmp(text, d, sz) == 0);
                free(d);
                free(c);
            } else {
                TEST("level diff: compress ok", 0);
            }
        }
        /* Monotone-ish check: lv1 must be strictly larger than lv9 for
         * periodic text -- this is the regression we are guarding. */
        TEST("level diff: lv1 > lv9 (regression guard)",
             sizes[1] > sizes[9]);
        /* Higher levels must never regress against level 1 by more than
         * 1% of input -- lazy matching is allowed to find a slightly
         * different match layout. */
        for (int lv = 2; lv <= 9; lv++) {
            if (rc[lv] > 0 && sizes[lv] > sizes[1] + sz / 100) {
                fprintf(stderr,
                        "FAIL: level %d (%zu) is much larger than level 1 (%zu)\n",
                        lv, sizes[lv], sizes[1]);
                failures++;
            }
        }
        free(text);
    }

    /* Test 5c: v2 match-token 0x81 冲突回归
     *
     * v2 match token 偏移 0x200-0x3FF 时首字节为 0x81，与转义字面量标记
     * (0x81 <val>) 冲突，解码器会将其误判为转义字面量 → 数据损坏（提前
     * 命中结束标记 / 内容错乱）。压缩侧已避免编码该偏移范围的匹配；
     * 本测试用周期 600 的数据（匹配偏移恰落 0x200-0x3FF）确保各压缩级别
     * 均能完整往返。 */
    {
        const size_t sz = 3000;
        const size_t period = 600;   /* 匹配偏移 = 600 ∈ [512,1023] */
        uint8_t *data = (uint8_t *)malloc(sz);
        for (size_t i = 0; i < sz; i++)
            data[i] = (uint8_t)(i % period);

        for (int lv = 1; lv <= 9; lv++) {
            void *c = NULL; size_t cl = 0;
            int r = mz_compress(data, sz, &c, &cl, MZ_CODEC_LZ77, lv);
            TEST("0x81-collision: compress ok", r > 0);
            if (r > 0) {
                void *d = NULL; size_t dl = 0;
                r = mz_decompress(c, cl, &d, &dl, MZ_CODEC_LZ77);
                TEST("0x81-collision: roundtrip len", r > 0 && dl == sz);
                TEST("0x81-collision: roundtrip data",
                     r > 0 && dl == sz && memcmp(data, d, sz) == 0);
                free(d);
            }
            free(c);
        }
        free(data);
    }

    /* Test 6: Error handling */
    uint8_t bad_data[4] = {0, 0, 0, 0};
    ret = mz_decompress(bad_data, 4, &decompressed, &decomp_len,
                        MZ_CODEC_LZ77);
    TEST("bad data returns error", ret < 0);

    ret = mz_compress(text, text_len, &compressed, &comp_len, 999, 1);
    TEST("bad codec returns error", ret == MZ_ERR_CODEC);
    
    printf("\n%d test(s) FAILED\n", failures);
    return failures > 0 ? 1 : 0;
}
