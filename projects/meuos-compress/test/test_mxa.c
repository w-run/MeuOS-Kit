#include "mxa.h"
#include "mz_ed25519.h"
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

    /* Test 6: 带 Ed25519 签名的归档 */
    {
        uint8_t seed[32];
        for (int i = 0; i < 32; i++) seed[i] = (uint8_t)(i * 17 + 42);
        uint8_t sk[MZ_ED25519_SK_LEN], pk[MZ_ED25519_PK_LEN];

        if (mz_ed25519_keypair(seed, sk, pk) == 0) {
            void *ctx = NULL, *result = NULL;
            size_t result_len = 0;
            struct mxa_params params = {0};
            params.level = 1;
            params.sk = seed;
            params.flags |= MXA_FLAG_SIGNED;

            mxa_create(&ctx, &params);
            mxa_add_file(ctx, "signed.txt", "signed data", 11, 0644, 0, 0, 0);
            mxa_finish(ctx, &result, &result_len);
            TEST("signed: archive created", result_len > 0);

            void *rctx = NULL;
            mxa_open(result, result_len, &rctx);
            int rc = mxa_verify(rctx, pk);
            TEST("signed: verify OK", rc == MXA_OK);

            /* 篡改后验证应失败 */
            uint8_t *tampered = malloc(result_len);
            memcpy(tampered, result, result_len);
            if (result_len > 100) tampered[100] ^= 0xFF;
            void *rctx2 = NULL;
            mxa_open(tampered, result_len, &rctx2);
            rc = mxa_verify(rctx2, pk);
            TEST("signed: tampered detected", rc != MXA_OK);
            mxa_close(rctx2);
            free(tampered);
            mxa_close(rctx);
            free(result);
        } else {
            /* libsodium not available -- skip */
            TEST("signed: SKIP (no libsodium)", 1);
        }
    }

    /* Test 7: ChaCha20 加密归档 */
    {
        uint8_t key[32];
        for (int i = 0; i < 32; i++) key[i] = (uint8_t)i;

        struct mxa_params params = {0};
        params.level = 6;
        params.key = key;
        params.flags |= MXA_FLAG_ENCRYPTED;

        void *ctx = NULL, *result = NULL;
        size_t result_len = 0;
        mxa_create(&ctx, &params);
        const char *secret = "This is secret data";
        mxa_add_file(ctx, "secret.txt", secret, strlen(secret)+1, 0600, 0, 0, 0);
        mxa_finish(ctx, &result, &result_len);
        TEST("encrypted: archive created", result_len > 0);

        /* 明文不应在归档中 */
        int found_plaintext = 0;
        const uint8_t *rp = (const uint8_t *)result;
        for (size_t i = 0; i + 5 < result_len; i++) {
            if (memcmp(rp + i, "This is secret data", 18) == 0) { found_plaintext = 1; break; }
        }
        TEST("encrypted: no plaintext in archive", !found_plaintext);

        /* 用正确密钥解密 */
        void *rctx = NULL;
        mxa_open(result, result_len, &rctx);
        mxa_set_key(rctx, key);
        void *d = NULL; size_t ds = 0;
        int rc = mxa_read_file(rctx, "secret.txt", &d, &ds);
        TEST("encrypted: read success", rc == MXA_OK);
        TEST("encrypted: roundtrip size", ds == strlen(secret)+1);
        TEST("encrypted: roundtrip content",
             ds > 0 && memcmp(d, secret, ds) == 0);
        free(d);
        mxa_close(rctx);

        /* 用错误密钥应解压失败 */
        uint8_t wrong_key[32];
        memset(wrong_key, 0xFF, 32);
        void *rctx3 = NULL;
        mxa_open(result, result_len, &rctx3);
        mxa_set_key(rctx3, wrong_key);
        d = NULL; ds = 0;
        rc = mxa_read_file(rctx3, "secret.txt", &d, &ds);
        TEST("encrypted: wrong key fails", rc != MXA_OK);
        mxa_close(rctx3);
        free(result);
    }

    /* Test 8: 加密 + 签名组合 */
    {
        uint8_t seed[32], key[32];
        for (int i = 0; i < 32; i++) { seed[i] = (uint8_t)(i * 31); key[i] = (uint8_t)(i * 17); }
        uint8_t sk[MZ_ED25519_SK_LEN], pk[MZ_ED25519_PK_LEN];

        int can_sign = (mz_ed25519_keypair(seed, sk, pk) == 0);

        struct mxa_params params = {0};
        params.level = 6;
        params.key = key;
        params.flags |= MXA_FLAG_SIGNED | MXA_FLAG_ENCRYPTED;
        if (can_sign) params.sk = seed;

        void *ctx = NULL, *result = NULL;
        size_t result_len = 0;
        mxa_create(&ctx, &params);
        mxa_add_file(ctx, "dual.txt", "encrypted and signed", 20, 0644, 0, 0, 0);
        mxa_finish(ctx, &result, &result_len);
        TEST("dual: archive created", result_len > 0);

        void *rctx = NULL;
        mxa_open(result, result_len, &rctx);
        mxa_set_key(rctx, key);
        void *d = NULL; size_t ds = 0;
        int rc = mxa_read_file(rctx, "dual.txt", &d, &ds);
        TEST("dual: read success", rc == MXA_OK);
        TEST("dual: roundtrip", ds == 20 && memcmp(d, "encrypted and signed", 20) == 0);
        free(d);

        if (can_sign) {
            rc = mxa_verify(rctx, pk);
            TEST("dual: verify OK", rc == MXA_OK);
        } else {
            TEST("dual: verify SKIP (no libsodium)", 1);
        }
        mxa_close(rctx);
        free(result);
    }

    printf("\n%d test(s) FAILED\n", failures);
    return failures ? 1 : 0;
}
