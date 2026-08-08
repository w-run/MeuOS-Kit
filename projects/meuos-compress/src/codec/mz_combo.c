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
 *   lv=0:  自适应模式 — 引擎自动选择 level 和熵编码
 */
#include "mz.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define COMBO_TYPE_RAW    0
#define COMBO_TYPE_HUFF   1
#define COMBO_TYPE_TANS   2

/* -------------------------------------------------------------------
 * 自适应调度 — 熵估计与 level 自动选择
 * ------------------------------------------------------------------- */

/* 快速字节熵估计：采样前 SAMPLE_BYTES 字节，计算比特/字节熵值
 * 返回 0.0 ~ 8.0 的定点数（乘以 100 避免浮点），近似 bits per byte
 * 足够用于 level 调度决策。 */
#define ENTROPY_SAMPLE_MAX  4096
#define ENTROPY_SCALE       100

static int
estimate_entropy_bpp100(const uint8_t *data, size_t len)
{
    if (len == 0) return 0;

    size_t sample = len < ENTROPY_SAMPLE_MAX ? len : ENTROPY_SAMPLE_MAX;
    unsigned freq[256];
    memset(freq, 0, sizeof(freq));

    for (size_t i = 0; i < sample; i++)
        freq[data[i]]++;

    /* 计算香农熵: H = -sum(p * log2(p)) */
    /* 使用定点数：freq 缩放为 Q16 后查 log2 近似表 */
    /* 简化: 用"活跃符号数" + "重复度" 做快速估计 */
    int used = 0;
    int max_freq = 0;
    uint64_t sum = 0;
    for (int i = 0; i < 256; i++) {
        if (freq[i] > 0) {
            used++;
            sum += (uint64_t)freq[i];
            if ((unsigned)freq[i] > (unsigned)max_freq) max_freq = (int)freq[i];
        }
    }

    if (used <= 1) return 0;                       /* 全相同 → 极低熵 */
    if (sum == 0) return 0;

    /* 活跃符号占比 × 100 作为粗略熵估计：
     * 少量符号 → 低熵（可压缩），大量符号 → 高熵（难压缩） */
    int bpp = (used * ENTROPY_SCALE * ENTROPY_SCALE) / (int)sum;
    /* 再用最大频次占比修正：单一符号占比高 → 更低熵 */
    int max_pct = (max_freq * ENTROPY_SCALE) / (int)sum;
    if (max_pct > 50) {
        bpp = bpp * 60 / 100;  /* 有一个符号占大半 → 更易压缩 */
    }

    if (bpp > 8 * ENTROPY_SCALE) bpp = 8 * ENTROPY_SCALE;
    return bpp;
}

/* 检测数据是否为高度周期性的快速测试
 * 采样前 512 字节，看是否有短周期重复 */
static int
is_periodic(const uint8_t *data, size_t len)
{
    size_t sample = len < 512 ? len : 512;
    if (sample < 8) return 0;

    /* 试周期长度 3~64 */
    for (int p = 3; p <= 64 && p * 2 <= (int)sample; p++) {
        int match = 1;
        for (size_t i = p; i < sample; i++) {
            if (data[i] != data[i - p]) {
                match = 0;
                break;
            }
        }
        if (match) return 1;
    }
    return 0;
}

/* 自适应 level 选择：
 *   level=0 时根据数据特征自动选择最佳 level */
static int
adaptive_select_level(const uint8_t *data, size_t len)
{
    /* 小文件（<4KB）直接 RAW 存储，免压缩开销 */
    if (len < 4096)
        return 1;  /* level 1 实际上在组合管线中被降级为 RAW */

    /* 检测周期性 */
    if (is_periodic(data, len))
        return 9;  /* 高周期性 → 深度匹配极大收益 */

    int bpp100 = estimate_entropy_bpp100(data, len);

    if (bpp100 > 700)   /* >7.0 bits/byte → 接近随机 */
        return 2;       /* 快速 LZ77 only */
    else if (bpp100 > 450)  /* 4.5~7.0 → 中等压缩 */
        return 5;       /* Huffman 模式 */
    else if (bpp100 > 200)  /* 2.0~4.5 → 好压缩 */
        return 7;       /* tANS 深度压缩 */
    else                    /* <2.0 → 极低熵 */
        return 9;       /* 极限压缩 */
}

/* -------------------------------------------------------------------
 * mz_compress_meuos — 统一入口（支持 level=0 自适应）
 * ------------------------------------------------------------------- */

int
mz_compress_meuos(const void *in, size_t il, void **r, size_t *rl, int lv)
{
    if (!in || !r || !rl) return MZ_ERR_PARAM;

    /* 自适应 level 选择 */
    if (lv == 0)
        lv = adaptive_select_level((const uint8_t *)in, il);

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
