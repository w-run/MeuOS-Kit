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

    /* Test 6: Error handling */
    uint8_t bad_data[4] = {0, 0, 0, 0};
    ret = mz_decompress(bad_data, 4, &decompressed, &decomp_len,
                        MZ_CODEC_LZ77);
    TEST("bad data returns error", ret < 0);

    ret = mz_compress(text, text_len, &compressed, &comp_len, 999, 1);
    TEST("bad codec returns error", ret == MZ_ERR_CODEC);

    /* Test 7: CRC-32 checksum — known test vectors */
    {
        /* CRC-32 of empty string = 0 */
        uint32_t crc = mz_crc32("", 0);
        TEST("crc32(empty)=0", crc == 0x00000000u);

        /* CRC-32 of "123456789" = 0xCBF43926 (IEEE 802.3, zlib test vector) */
        crc = mz_crc32("123456789", 9);
        TEST("crc32('123456789')=0xCBF43926", crc == 0xCBF43926u);

        /* CRC-32 of "abc" = 0x352441C2 */
        crc = mz_crc32("abc", 3);
        TEST("crc32('abc')=0x352441C2", crc == 0x352441C2u);

        /* CRC-32 of "a" = 0xE8B7BE43 */
        crc = mz_crc32("a", 1);
        TEST("crc32('a')=0xE8B7BE43", crc == 0xE8B7BE43u);

        /* Incremental vs one-shot consistency */
        const char *long_str =
            "The quick brown fox jumps over the lazy dog. "
            "Lorem ipsum dolor sit amet, consectetur adipiscing elit.";
        size_t slen = strlen(long_str);
        uint32_t crc_one = mz_crc32(long_str, slen);

        uint32_t crc_inc = mz_crc32_init();
        /* Feed in chunks of 16 bytes */
        size_t off = 0;
        while (off < slen) {
            size_t chunk = slen - off > 16 ? 16 : slen - off;
            crc_inc = mz_crc32_update(crc_inc, long_str + off, chunk);
            off += chunk;
        }
        crc_inc = mz_crc32_final(crc_inc);
        TEST("crc32 incremental == one-shot", crc_inc == crc_one);

        /* Split at arbitrary boundary */
        size_t mid = slen / 3;
        crc_inc = mz_crc32_init();
        crc_inc = mz_crc32_update(crc_inc, long_str, mid);
        crc_inc = mz_crc32_update(crc_inc, long_str + mid, slen - mid);
        crc_inc = mz_crc32_final(crc_inc);
        TEST("crc32 split boundary == one-shot", crc_inc == crc_one);

        printf("  crc32('123456789') = %08x (expected CBF43926)\n", mz_crc32("123456789", 9));
    }

    /* Test 8: Adler-32 checksum — known test vectors */
    {
        /* Adler-32 of empty string = 1 (s1=1, s2=0) */
        uint32_t adler = mz_adler32("", 0);
        TEST("adler32(empty)=1", adler == 0x00000001u);

        /* Adler-32 of "Wikipedia" = 0x11E60398 */
        adler = mz_adler32("Wikipedia", 9);
        TEST("adler32('Wikipedia')=0x11E60398", adler == 0x11E60398u);

        /* Adler-32 of "123456789" */
        adler = mz_adler32("123456789", 9);
        TEST("adler32('123456789')=0x091E01DE", adler == 0x091E01DEu);

        /* Incremental vs one-shot consistency */
        const char *long_str =
            "The quick brown fox jumps over the lazy dog. "
            "Lorem ipsum dolor sit amet, consectetur adipiscing elit.";
        size_t slen = strlen(long_str);
        uint32_t adler_one = mz_adler32(long_str, slen);

        uint32_t adler_inc = mz_adler32_init();
        size_t off = 0;
        while (off < slen) {
            size_t chunk = slen - off > 16 ? 16 : slen - off;
            adler_inc = mz_adler32_update(adler_inc, long_str + off, chunk);
            off += chunk;
        }
        adler_inc = mz_adler32_final(adler_inc);
        TEST("adler32 incremental == one-shot", adler_inc == adler_one);

        /* Large data wrap test — ensure NMAX boundary is correct */
        {
            size_t big_sz = 60000;  /* > NMAX (5552) */
            uint8_t *big = (uint8_t *)malloc(big_sz);
            for (size_t i = 0; i < big_sz; i++)
                big[i] = (uint8_t)(i & 0xFF);

            uint32_t a_one = mz_adler32(big, big_sz);
            uint32_t a_inc = mz_adler32_init();
            off = 0;
            while (off < big_sz) {
                size_t chunk = big_sz - off > 5552 ? 5552 : big_sz - off;
                a_inc = mz_adler32_update(a_inc, big + off, chunk);
                off += chunk;
            }
            a_inc = mz_adler32_final(a_inc);
            TEST("adler32 large NMAX boundary == one-shot", a_inc == a_one);

            free(big);
        }

        printf("  adler32('Wikipedia') = %08x (expected 11E60398)\n",
               mz_adler32("Wikipedia", 9));
    }

    printf("\n%d test(s) FAILED\n", failures);
    return failures > 0 ? 1 : 0;
}
