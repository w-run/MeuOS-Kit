#include "mxa.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int failures = 0;
#define TEST(name, cond) do {                              \
        if (!(cond)) {                                      \
            fprintf(stderr, "FAIL: %s\n", name);            \
            failures++;                                     \
        } else {                                            \
            printf("PASS: %s\n", name);                     \
        }                                                   \
    } while (0)

int main(void)
{
    /* Test 1: 创建空归档 */
    {
        void *ctx = NULL, *result = NULL;
        size_t result_len = 0;
        struct mxa_params params = {0};
        params.level = 1;

        int rc = mxa_create(&ctx, &params);
        TEST("create empty ctx", rc == MXA_OK);

        rc = mxa_finish(ctx, &result, &result_len);
        TEST("finish empty archive", rc == MXA_OK);
        TEST("empty archive size > 0", result_len > 0);

        void *rctx = NULL;
        rc = mxa_open(result, result_len, &rctx);
        TEST("open empty archive", rc == MXA_OK);

        struct mxa_file_entry *entries = NULL;
        int count = 0;
        rc = mxa_list_files(rctx, &entries, &count);
        TEST("list files on empty archive", rc == MXA_OK);
        TEST("empty archive has 0 files", count == 0);

        mxa_close(rctx);
        free(entries);
        free(result);
    }

    /* Test 2: 单文件归档 */
    {
        void *ctx = NULL, *result = NULL;
        size_t result_len = 0;
        struct mxa_params params = {0};
        params.level = 1;

        const char *content = "Hello, MeuOS Archive!";
        size_t content_len = strlen(content) + 1;

        mxa_create(&ctx, &params);
        mxa_add_file(ctx, "hello.txt", content, content_len,
                     0644, 1000, 1000, 0);
        mxa_finish(ctx, &result, &result_len);
        TEST("single file: result > 0", result_len > 0);

        void *rctx = NULL;
        mxa_open(result, result_len, &rctx);

        struct mxa_file_entry *entries = NULL;
        int count = 0;
        mxa_list_files(rctx, &entries, &count);
        TEST("single file: count == 1", count == 1);
        TEST("single file: correct name",
             count > 0 && strcmp(entries[0].name, "hello.txt") == 0);
        TEST("single file: correct mode",
             count > 0 && entries[0].mode == 0644);
        TEST("single file: correct size",
             count > 0 && entries[0].size == content_len);

        void *data = NULL;
        size_t dsize = 0;
        int rc = mxa_read_file(rctx, "hello.txt", &data, &dsize);
        TEST("read file: success", rc == MXA_OK);
        TEST("read file: size matches", dsize == content_len);
        TEST("read file: content matches",
             dsize > 0 && memcmp(data, content, dsize) == 0);

        free(data);
        mxa_close(rctx);
        free(entries);
        free(result);
    }

    /* Test 3: 多文件归档 */
    {
        void *ctx = NULL, *result = NULL;
        size_t result_len = 0;
        struct mxa_params params = {0};
        params.level = 1;

        mxa_create(&ctx, &params);
        mxa_add_file(ctx, "a.txt", "AAAA", 4, 0644, 0, 0, 0);
        mxa_add_file(ctx, "b.txt", "BBBB", 4, 0644, 0, 0, 0);
        mxa_add_file(ctx, "c.txt", "CCCC", 4, 0644, 0, 0, 0);
        mxa_finish(ctx, &result, &result_len);
        TEST("multi file: result > 0", result_len > 0);

        void *rctx = NULL;
        mxa_open(result, result_len, &rctx);

        struct mxa_file_entry *entries = NULL;
        int count = 0;
        mxa_list_files(rctx, &entries, &count);
        TEST("multi file: count == 3", count == 3);

        const char *names[] = {"a.txt", "b.txt", "c.txt"};
        const char *data[]  = {"AAAA", "BBBB", "CCCC"};
        for (int i = 0; i < 3; i++) {
            void *d = NULL;
            size_t ds = 0;
            int rc = mxa_read_file(rctx, names[i], &d, &ds);
            TEST("multi file: read success", rc == MXA_OK);
            TEST("multi file: correct len", ds == 4);
            TEST("multi file: correct content",
                 memcmp(d, data[i], 4) == 0);
            free(d);
        }

        void *d = NULL;
        size_t ds = 0;
        int rc = mxa_read_file(rctx, "nonexistent.txt", &d, &ds);
        TEST("non-existent file returns NOTFOUND",
             rc == MXA_ERR_NOTFOUND);

        mxa_close(rctx);
        free(entries);
        free(result);
    }

    /* Test 4: 大文件归档 (100KB 周期文本) */
    {
        void *ctx = NULL, *result = NULL;
        size_t result_len = 0;
        struct mxa_params params = {0};
        params.level = 6;

        size_t sz = 100000;
        char *big = malloc(sz);
        for (size_t i = 0; i < sz; i++)
            big[i] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[i % 26];

        mxa_create(&ctx, &params);
        mxa_add_file(ctx, "large.bin", big, sz, 0644, 0, 0, 0);
        mxa_finish(ctx, &result, &result_len);
        TEST("large file: compressed smaller", result_len < sz);
        printf("  large file: %zu -> %zu (%.1f%%)\n",
               sz, result_len, 100.0 * result_len / sz);

        void *rctx = NULL;
        mxa_open(result, result_len, &rctx);

        void *d = NULL;
        size_t ds = 0;
        mxa_read_file(rctx, "large.bin", &d, &ds);
        TEST("large file: roundtrip size", ds == sz);
        TEST("large file: roundtrip content",
             memcmp(big, d, sz) == 0);

        free(d);
        free(big);
        mxa_close(rctx);
        free(result);
    }

    /* Test 5: STORED 模式（小文件自动存储） */
    {
        void *ctx = NULL, *result = NULL;
        size_t result_len = 0;
        struct mxa_params params = {0};
        params.level = 1;

        const char *content = "0123456789";
        size_t clen = 10;

        mxa_create(&ctx, &params);
        mxa_add_file(ctx, "stored.bin", content, clen,
                     0644, 0, 0, 0);
        mxa_finish(ctx, &result, &result_len);

        void *rctx = NULL;
        mxa_open(result, result_len, &rctx);

        struct mxa_file_entry *entries = NULL;
        int count = 0;
        mxa_list_files(rctx, &entries, &count);
        TEST("stored: count > 0", count > 0);

        void *d = NULL;
        size_t ds = 0;
        mxa_read_file(rctx, "stored.bin", &d, &ds);
        TEST("stored: roundtrip size", ds == clen);
        TEST("stored: roundtrip content",
             memcmp(content, d, clen) == 0);

        free(d);
        mxa_close(rctx);
        free(entries);
        free(result);
    }

    printf("\n%d test(s) FAILED\n", failures);
    return failures ? 1 : 0;
}
