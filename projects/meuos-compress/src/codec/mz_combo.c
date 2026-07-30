/* mz_combo.c — LZ77 + 熵编码组合压缩管线
 *
 * 格式:
 *   [0]      : entropy_type (0=none, 1=Huffman, 2=tANS)
 *   [1..]    : 熵编码后的数据（熵编码解码后得到 LZ77 数据）
 *
 * 级别映射:
 *   lv1-3: LZ77 only (raw)
 *   lv4-6: LZ77 + Huffman
 *   lv7-9: LZ77 + tANS
 */
#include "mz.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define COMBO_TYPE_RAW    0
#define COMBO_TYPE_HUFF   1
#define COMBO_TYPE_TANS   2

int
mz_compress_meuos(const void *in, size_t il, void **r, size_t *rl, int lv)
{
    if (!in || !r || !rl) return MZ_ERR_PARAM;

    /* Step 1: LZ77 compress */
    void *lz77 = NULL;
    size_t lz77l = 0;
    int rc = mz_compress_lz77(in, il, &lz77, &lz77l, lv);
    if (rc <= 0) return rc;

    /* Step 2: Apply entropy coding for levels >= 4 */
    int etype = COMBO_TYPE_RAW;
    void *ecode = NULL;
    size_t ecode_len = 0;

    if (lv >= 7) {
        /* Try tANS */
        size_t max_out = lz77l + lz77l / 4 + 4096;
        unsigned char *buf = (unsigned char *)malloc(max_out);
        if (buf) {
            size_t outl = max_out;
            if (mz_tans_compress((const unsigned char *)lz77, lz77l,
                                 buf, &outl) == 0 && outl < lz77l) {
                ecode = buf;
                ecode_len = outl;
                etype = COMBO_TYPE_TANS;
            } else {
                free(buf);
            }
        }
    } else if (lv >= 4) {
        /* Try Huffman */
        size_t max_out = lz77l + lz77l / 4 + 4096;
        unsigned char *buf = (unsigned char *)malloc(max_out);
        if (buf) {
            size_t outl = max_out;
            if (mz_huf_compress((const unsigned char *)lz77, lz77l,
                                buf, &outl) == 0 && outl < lz77l) {
                ecode = buf;
                ecode_len = outl;
                etype = COMBO_TYPE_HUFF;
            } else {
                free(buf);
            }
        }
    }

    /* Step 3: Build output */
    size_t total = 1 + (etype != COMBO_TYPE_RAW ? ecode_len : lz77l);
    unsigned char *out = (unsigned char *)malloc(total);
    if (!out) {
        free(lz77);
        free(ecode);
        return MZ_ERR_MEMORY;
    }

    out[0] = (unsigned char)etype;
    if (etype != COMBO_TYPE_RAW) {
        memcpy(out + 1, ecode, ecode_len);
        free(ecode);
    } else {
        memcpy(out + 1, lz77, lz77l);
    }
    free(lz77);

    *r = out;
    *rl = total;
    return (int)total;
}

int
mz_decompress_meuos(const void *in, size_t il, void **r, size_t *rl)
{
    if (!in || !r || !rl || il < 1) return MZ_ERR_PARAM;

    const unsigned char *p = (const unsigned char *)in;
    int etype = p[0];
    const void *data = p + 1;
    size_t data_len = il - 1;

    /* Step 1: Decompress entropy layer to get LZ77 data */
    void *lz77 = NULL;
    size_t lz77l = 0;

    if (etype == COMBO_TYPE_TANS) {
        size_t max_out = data_len * 4 + 4096;
        unsigned char *buf = (unsigned char *)malloc(max_out);
        if (!buf) return MZ_ERR_MEMORY;
        size_t outl = max_out;
        if (mz_tans_decompress(data, data_len, buf, &outl) != 0) {
            free(buf);
            return MZ_ERR_DATA;
        }
        lz77 = buf;
        lz77l = outl;
    } else if (etype == COMBO_TYPE_HUFF) {
        size_t max_out = data_len * 4 + 4096;
        unsigned char *buf = (unsigned char *)malloc(max_out);
        if (!buf) return MZ_ERR_MEMORY;
        size_t outl = max_out;
        if (mz_huf_decompress(data, data_len, buf, &outl) != 0) {
            free(buf);
            return MZ_ERR_DATA;
        }
        lz77 = buf;
        lz77l = outl;
    } else if (etype == COMBO_TYPE_RAW) {
        lz77 = (void *)data;  /* no copy needed */
        lz77l = data_len;
    } else {
        return MZ_ERR_DATA;
    }

    /* Step 2: LZ77 decompress */
    int rc = mz_decompress_lz77(lz77, lz77l, r, rl);

    if (etype != COMBO_TYPE_RAW) free(lz77);
    return rc;
}
