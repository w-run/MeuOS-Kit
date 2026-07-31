/* mz_ans.c — tANS (table Asymmetric Numeral Systems) codec
 *
 * 最简化但保证 roundtrip 正确的 tANS 实现 (参考 FSE 风格).
 *
 * 关键设计:
 *   - 状态空间 [0, L), L = 256, ANS_L_BITS = 8
 *   - norm[s] 归一化到 1, 2, 4, 8, ..., 256 (2 的幂), sum = 256
 *   - 状态机 (FSE 风格):
 *     enc: state = (state >> nb) + base_offset[sym]
 *          其中 nb = compute_nb(f), base_offset[sym] = sym 槽的块起始
 *     dec: state = (state & ~mask) + bits, where mask = (1<<nb)-1
 *          即 new_state = (state >> nb) << nb + bits
 *
 * 编码 (反向处理 in[n-1] → in[0]):
 *   state = L (起始)
 *   for each sym (反向):
 *     while state >= f * 2^nb:  // 重正化
 *       write state LSB (1 bit)
 *       state >>= 1
 *     bits = state & ((1 << nb) - 1)
 *     r = state >> nb
 *     state = slot[sym][r]  // 新 state
 *     write bits (nb bits)
 *   final state → header
 *
 * 解码 (正向输出 out[0] → out[n-1]):
 *   state = final state (from header)
 *   for each sym (正向):
 *     sym = symbol[state]
 *     nb = nb_bits[state]
 *     read nb bits → bits
 *     state = (state >> nb) << nb + bits  // (= (state & ~mask) + bits)
 *     output sym
 *
 * File format (all little-endian):
 *   [0..512)    : 256 normalized frequencies (uint16_t each, sum = 256)
 *   [512..516)  : final state (uint32_t, value in [0, 256))
 *   [516..520)  : total bit count of bitstream (uint32_t)
 *   [520..)     : bitstream bytes
 *
 * Bitstream convention:
 *   bitContainer = (bitContainer << n) | value
 *   先编码的 value 在 high bit. byte[0] = 最后编码的 value 的 low 8 bit.
 *   解码从 byte[0] 开始 LSB-first 读.
 */

#include "mz.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define ANS_L       256
#define ANS_L_BITS  8

struct ans_tables {
    uint16_t norm[256];
    uint8_t  symbol[256];
    uint8_t  rank_tab[256];       /* spread 顺序的 rank in sym */
    uint8_t  nb_bits[256];
    uint16_t base[256];
    uint8_t  slot[256][256];
};

/* ---- frequency normalisation to 2^nb ----
 * 强制 norm[s] ∈ {1, 2, 4, 8, ..., 256}. 调整 sum 到 256.
 */
static int
normalise_freqs(const unsigned raw[256], uint16_t norm[256])
{
    uint64_t total = 0;
    int used = 0;
    for (int i = 0; i < 256; i++) {
        total += raw[i];
        if (raw[i] > 0) used++;
    }
    if (total == 0) return MZ_ERR_DATA;

    if (used == 1) {
        for (int i = 0; i < 256; i++) norm[i] = 0;
        for (int i = 0; i < 256; i++) {
            if (raw[i] > 0) { norm[i] = ANS_L; break; }
        }
        return MZ_OK;
    }

    /* 第一遍: 圆整到 2^nb */
    int sum = 0;
    for (int i = 0; i < 256; i++) {
        if (raw[i] == 0) {
            norm[i] = 0;
        } else {
            uint64_t v = (raw[i] * ANS_L) / total;
            if (v == 0) v = 1;
            int p = 1;
            while (((uint64_t)p << 1) <= v && (p << 1) <= ANS_L) p <<= 1;
            norm[i] = (uint16_t)p;
            sum += p;
        }
    }

    /* 第二遍: 调整 sum 到 256. 优先保持 2^nb (×2 或 /2). */
    {
        int safety = 10000;
        while (safety-- > 0) {
            int sum_now = 0;
            for (int i = 0; i < 256; i++) sum_now += norm[i];
            int diff = (int)ANS_L - sum_now;
            if (diff == 0) break;
            int changed = 0;
            if (diff > 0) {
                for (int i = 0; i < 256 && diff > 0; i++) {
                    if (norm[i] > 0 && (uint32_t)norm[i] * 2 <= ANS_L) {
                        int delta = norm[i];
                        norm[i] = (uint16_t)(norm[i] * 2);
                        diff -= delta;
                        changed = 1;
                    }
                }
                for (int i = 0; i < 256 && diff > 0; i++) {
                    if (norm[i] > 0 && norm[i] < ANS_L) {
                        norm[i] = (uint16_t)(norm[i] + 1);
                        diff--;
                        changed = 1;
                    }
                }
            } else {
                for (int i = 255; i >= 0 && diff < 0; i--) {
                    if (norm[i] > 1 && norm[i] % 2 == 0) {
                        int delta = norm[i] / 2;
                        norm[i] = (uint16_t)(norm[i] / 2);
                        diff += delta;
                        changed = 1;
                    }
                }
                for (int i = 255; i >= 0 && diff < 0; i--) {
                    if (norm[i] > 1) {
                        norm[i] = (uint16_t)(norm[i] - 1);
                        diff++;
                        changed = 1;
                    }
                }
            }
            if (!changed) break;
        }
    }

    /* 验证: 每个非零 norm 是 2 的幂 */
    for (int i = 0; i < 256; i++) {
        if (norm[i] > 0 && (norm[i] & (norm[i] - 1))) {
            /* 不是 2 的幂, 强制 */
            int p = 1;
            while (p < norm[i] && p < ANS_L) p <<= 1;
            norm[i] = (uint16_t)p;
        }
    }
    return MZ_OK;
}

static int
compute_nb(int f)
{
    if (f >= ANS_L) return 0;
    int nb = 0;
    while ((f << 1) <= ANS_L) { f <<= 1; nb++; }
    return nb;
}

/* 按 nb 从大到小排序 (norm 大的 nb 小) */
static void
sort_syms_by_nb(int order[256], const uint16_t norm[256])
{
    int n = 0;
    for (int i = 0; i < 256; i++) {
        if (norm[i] > 0) order[n++] = i;
    }
    for (int i = 1; i < n; i++) {
        int k = order[i];
        int nbk = compute_nb(norm[k]);
        int j = i - 1;
        while (j >= 0) {
            int nbj = compute_nb(norm[order[j]]);
            if (nbj > nbk) break;
            if (nbj == nbk && order[j] < k) break;
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = k;
    }
    for (int i = n; i < 256; i++) order[i] = -1;
}

/* 构造表：FSE-style spread。
 * 注意: 因为 norm 强制为 2^nb, 且 spread 是 "顺序放" (i.e., sym 的 slot 连续),
 * 但不同 sym 的 nb 不同, slot 大小不同, 所以 slot 不一定连续.
 * 关键: rank 仍然由 spread 顺序的索引 i 决定.
 * base = i * 2^nb (按 spread 顺序 rank, 不按 state >> nb).
 */
static int
build_tables(struct ans_tables *t, const uint16_t norm[256])
{
    /* Validate the frequency table before building anything.  The table
     * comes from the compressed stream on decompression and is attacker
     * controlled: a sum != ANS_L can spin the slot-filling loop forever,
     * and a single f > ANS_L overflows slot[s][i].  Accept only tables
     * where every nonzero f is a power of two (<= ANS_L) and they sum
     * exactly to ANS_L. */
    uint32_t norm_sum = 0;
    for (int i = 0; i < 256; i++) {
        uint16_t f = norm[i];
        if (f > ANS_L) return MZ_ERR_DATA;
        if (f && (f & (f - 1))) return MZ_ERR_DATA;  /* not a power of two */
        norm_sum += f;
    }
    if (norm_sum != ANS_L) return MZ_ERR_DATA;

    memcpy(t->norm, norm, sizeof(t->norm));
    memset(t->symbol, 0xFF, sizeof(t->symbol));
    memset(t->rank_tab, 0, sizeof(t->rank_tab));
    memset(t->nb_bits, 0, sizeof(t->nb_bits));
    memset(t->base, 0, sizeof(t->base));
    memset(t->slot, 0, sizeof(t->slot));

    int order[256];
    sort_syms_by_nb(order, norm);

    int step = (ANS_L >> 1) + (ANS_L >> 3) + 3;
    step |= 1;

    int pos = 0;
    for (int oi = 0; oi < 256 && order[oi] >= 0; oi++) {
        int s = order[oi];
        int f = norm[s];
        int nb = compute_nb(f);
        for (int i = 0; i < f; i++) {
            while (t->symbol[pos] != 0xFF)
                pos = (pos + 1) & (ANS_L - 1);
            t->symbol[pos] = (uint8_t)s;
            t->slot[s][i] = (uint8_t)pos;
            t->rank_tab[pos] = (uint8_t)i;
            t->nb_bits[pos] = (uint8_t)nb;
            t->base[pos] = (uint16_t)(i << nb);
            pos = (pos + step) & (ANS_L - 1);
        }
    }

    return MZ_OK;
}

/* ---- bitstream writer (FSE 风格) ---- */

struct ans_bs {
    uint8_t *buf;
    size_t   cap;
    size_t   byte_pos;
    uint32_t container;
    int      bit_count;
};

static int
bs_init(struct ans_bs *b, uint8_t *buf, size_t cap)
{
    b->buf = buf;
    b->cap = cap;
    b->byte_pos = 0;
    b->container = 0;
    b->bit_count = 0;
    return MZ_OK;
}

static int
bs_put(struct ans_bs *b, uint32_t value, int n)
{
    if (n <= 0) return MZ_OK;
    if (n > 32) return MZ_ERR_PARAM;
    b->container = (b->container << n) | (value & ((1u << n) - 1));
    b->bit_count += n;
    while (b->bit_count >= 8) {
        if (b->byte_pos >= b->cap) return MZ_ERR_STREAM;
        b->buf[b->byte_pos++] = (uint8_t)(b->container & 0xFF);
        b->container >>= 8;
        b->bit_count -= 8;
    }
    return MZ_OK;
}

static int
bs_flush(struct ans_bs *b)
{
    if (b->bit_count == 0) return MZ_OK;
    if (b->byte_pos >= b->cap) return MZ_ERR_STREAM;
    b->buf[b->byte_pos++] = (uint8_t)(b->container & 0xFF);
    b->container = 0;
    b->bit_count = 0;
    return MZ_OK;
}

struct ans_br {
    const uint8_t *buf;
    size_t   cap;
    size_t   bit_pos;
};

static void
br_init(struct ans_br *b, const uint8_t *buf, size_t cap)
{
    b->buf = buf;
    b->cap = cap;
    b->bit_pos = 0;
}

static int
br_get(struct ans_br *b)
{
    if ((b->bit_pos >> 3) >= b->cap) return -1;
    int bit = (b->buf[b->bit_pos >> 3] >> (b->bit_pos & 7)) & 1;
    b->bit_pos++;
    return bit;
}

static int
br_get_n(struct ans_br *b, int n, uint32_t *out)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) {
        int bit = br_get(b);
        if (bit < 0) return -1;
        v |= (uint32_t)bit << i;
    }
    *out = v;
    return 0;
}

/* ---- 编码 ---- */

int
mz_tans_compress(const unsigned char *in, size_t inlen,
                 unsigned char *out, size_t *outlen)
{
    if (!in || !out || !outlen) return MZ_ERR_PARAM;
    if (inlen == 0) return MZ_ERR_PARAM;

    unsigned freqs[256];
    memset(freqs, 0, sizeof(freqs));
    for (size_t i = 0; i < inlen; i++)
        freqs[in[i]]++;

    uint16_t norm[256];
    int ret = normalise_freqs(freqs, norm);
    if (ret != MZ_OK) return ret;

    struct ans_tables *tab = (struct ans_tables *)malloc(sizeof(*tab));
    if (!tab) return MZ_ERR_MEMORY;
    ret = build_tables(tab, norm);
    if (ret != MZ_OK) { free(tab); return ret; }

    size_t max_out = *outlen;
    if (520 > max_out) { free(tab); return MZ_ERR_STREAM; }
    size_t ip = 0;
    for (int i = 0; i < 256; i++) {
        out[ip++] = (uint8_t)(norm[i] & 0xFF);
        out[ip++] = (uint8_t)((norm[i] >> 8) & 0xFF);
    }
    size_t hdr_end = 520;
    size_t bs_cap = max_out - hdr_end;
    for (size_t i = 0; i < bs_cap; i++) out[hdr_end + i] = 0;

    struct ans_bs bw;
    bs_init(&bw, out + hdr_end, bs_cap);

    uint32_t state = ANS_L;
    size_t idx = inlen;
    while (idx > 0) {
        idx--;
        int sym = in[idx];
        int f = norm[sym];
        if (f == 0) { free(tab); return MZ_ERR_DATA; }
        int nb = compute_nb(f);

        uint32_t threshold = (uint32_t)f << nb;
        while (state >= threshold) {
            ret = bs_put(&bw, state & 1, 1);
            if (ret != MZ_OK) { free(tab); return ret; }
            state >>= 1;
        }

        int r = (int)(state >> nb);
        int bits = (int)(state & ((1u << nb) - 1));
        if (r >= f) { free(tab); return MZ_ERR_DATA; }
        state = tab->slot[sym][r];
        if (state >= ANS_L) { free(tab); return MZ_ERR_DATA; }

        ret = bs_put(&bw, (uint32_t)bits, nb);
        if (ret != MZ_OK) { free(tab); return ret; }
    }

    ret = bs_flush(&bw);
    if (ret != MZ_OK) { free(tab); return ret; }

    size_t bs_len = bw.byte_pos;

    out[512] = (uint8_t)(state & 0xFF);
    out[513] = (uint8_t)((state >> 8) & 0xFF);
    out[514] = (uint8_t)((state >> 16) & 0xFF);
    out[515] = (uint8_t)((state >> 24) & 0xFF);
    size_t total_bits = bs_len * 8;
    out[516] = (uint8_t)(total_bits & 0xFF);
    out[517] = (uint8_t)((total_bits >> 8) & 0xFF);
    out[518] = (uint8_t)((total_bits >> 16) & 0xFF);
    out[519] = (uint8_t)((total_bits >> 24) & 0xFF);

    *outlen = hdr_end + bs_len;
    free(tab);
    return MZ_OK;
}

/* ---- 解码 ---- */

int
mz_tans_decompress(const unsigned char *in, size_t inlen,
                   unsigned char *out, size_t *outlen)
{
    if (!in || !out || !outlen) return MZ_ERR_PARAM;
    if (inlen < 520) return MZ_ERR_DATA;

    size_t ip = 0;
    uint16_t norm[256];
    for (int i = 0; i < 256; i++) {
        norm[i] = (uint16_t)((uint32_t)in[ip]
                            | ((uint32_t)in[ip + 1] << 8));
        ip += 2;
    }

    struct ans_tables *tab = (struct ans_tables *)malloc(sizeof(*tab));
    if (!tab) return MZ_ERR_MEMORY;
    int ret = build_tables(tab, norm);
    if (ret != MZ_OK) { free(tab); return ret; }

    uint32_t state = (uint32_t)in[512]
                   | ((uint32_t)in[513] << 8)
                   | ((uint32_t)in[514] << 16)
                   | ((uint32_t)in[515] << 24);
    if (state >= ANS_L) { free(tab); return MZ_ERR_DATA; }

    struct ans_br br;
    br_init(&br, in + 520, inlen - 520);

    size_t olen = *outlen;
    for (size_t i = 0; i < olen; i++) {
        int x = (int)state;
        int sym = tab->symbol[x];
        if (sym < 0 || (int)tab->norm[sym] == 0) {
            free(tab); return MZ_ERR_DATA;
        }
        out[i] = (uint8_t)sym;
        int nb = tab->nb_bits[x];
        uint32_t bits = 0;
        if (nb > 0) {
            if (br_get_n(&br, nb, &bits) < 0) {
                free(tab); return MZ_ERR_DATA;
            }
        }
        /* new_state = (state & ~mask) + bits = rank * 2^nb + bits */
        uint32_t mask = ((1u << nb) - 1);
        uint32_t new_state = (state & ~mask) + bits;
        if (new_state >= ANS_L) { free(tab); return MZ_ERR_DATA; }
        state = new_state;
    }

    free(tab);
    return MZ_OK;
}
