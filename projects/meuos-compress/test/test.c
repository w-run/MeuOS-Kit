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
    
    
    /* Test 5: Error handling */
    uint8_t bad_data[4] = {0, 0, 0, 0};
    ret = mz_decompress(bad_data, 4, &decompressed, &decomp_len,
                        MZ_CODEC_LZ77);
    TEST("bad data returns error", ret < 0);
    
    ret = mz_compress(text, text_len, &compressed, &comp_len, 999, 1);
    TEST("bad codec returns error", ret == MZ_ERR_CODEC);
    
    printf("\n%d test(s) FAILED\n", failures);
    return failures > 0 ? 1 : 0;
}
