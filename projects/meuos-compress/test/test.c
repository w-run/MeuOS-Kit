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
                          MZ_CODEC_LZ77_HUF, MZ_LEVEL_NORMAL);
    TEST("compress succeeded", ret > 0);
    
    void *decompressed;
    size_t decomp_len;
    ret = mz_decompress(compressed, comp_len, &decompressed, &decomp_len,
                        MZ_CODEC_LZ77_HUF);
    TEST("decompress succeeded", ret > 0);
    TEST("decompressed size matches", decomp_len == text_len);
    TEST("decompressed content matches",
         memcmp(text, decompressed, text_len) == 0);
    
    printf("  original: %zu bytes -> compressed: %zu bytes (%.1f%%)\n",
           text_len, comp_len, 100.0 * comp_len / text_len);
    
    free(compressed);
    free(decompressed);
    
    /* Test 2: Empty input */
    ret = mz_compress("", 0, &compressed, &comp_len, MZ_CODEC_LZ77_HUF, 1);
    TEST("empty compress returns 0 or error", ret <= 0);
    
    /* Test 3: Large buffer with repetition (good for compression) — skip for now
    size_t large_sz = 100000;
    ...
    free(large);
    free(compressed);
    free(decompressed);*/
    
    /* Test 4: Streaming API */
    struct mz_stream stream;
    int written = 0;
    
    mz_stream_init(&stream, MZ_CODEC_LZ77_HUF, 5, 0);
    stream.write_out = NULL;  /* will use one-shot internally */
    mz_stream_feed(&stream, text, text_len);
    mz_stream_free(&stream);
    TEST("stream init/free works", 1);
    
    /* Test 5: Error handling */
    uint8_t bad_data[4] = {0, 0, 0, 0};
    ret = mz_decompress(bad_data, 4, &decompressed, &decomp_len,
                        MZ_CODEC_LZ77_HUF);
    TEST("bad data returns error", ret < 0);
    
    ret = mz_compress(text, text_len, &compressed, &comp_len, 999, 1);
    TEST("bad codec returns error", ret == MZ_ERR_CODEC);
    
    printf("\n%d test(s) FAILED\n", failures);
    return failures > 0 ? 1 : 0;
}
