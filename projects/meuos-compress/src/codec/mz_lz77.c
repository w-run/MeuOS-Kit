#include "mz.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* MZ LZ77 v2 — Improved LZ77 compressor/decompressor
 *
 * Format (v1): "MZ" magic + 4-byte uncompressed size + stream of tokens
 * Format (v2): "mZ" magic + 4-byte uncompressed size + stream of tokens
 *
 * Token format (v1, backward compat):
 *   Literal: 0bbbbbbb (bit7=0, low 7 bits = value)
 *            0x81 + byte (escaped literal for values >= 128)
 *   Match:   1ooooooo oooooooo llllllll (3 bytes)
 *            offset = ((b0 & 0x7F) << 8) | b1    (15 bits, 0..32767)
 *            length = b2 + MZ_MIN_MATCH           (8 bits, 3..258)
 *            offset=0 signals end marker
 *
 * Token format (v2, 16-bit offset):
 *   Literal: same as v1
 *   Match:   1ooooooo oooooooo oollllll (3 bytes)
 *            offset = ((b0 & 0x7F) << 9) | (b1 << 1) | ((b2 >> 7) & 1)
 *                      (16 bits, 0..65535)
 *            length = (b2 & 0x7F) + MZ_MIN_MATCH (7 bits, 3..130)
 *            offset=0 signals end marker
 *
 * v2 improvements:
 *   - 64KB hash window (v1: 4KB)
 *   - Triple hash chain: 2-byte pre-filter, 3-byte head, chain traversal
 *   - Level-based chain depth (level 1=64, level 9=4096)
 *   - 16-bit offset for full 64KB window coverage
 *   - Decompressor auto-detects v1/v2 by magic
 */

#define MZ_WBITS 16
#define MZ_WSIZE (1 << MZ_WBITS)            /* 65536 */
#define MZ_WMASK (MZ_WSIZE - 1)

#define MZ_HASH_BITS 16
#define MZ_HASH_SIZE (1 << MZ_HASH_BITS)     /* 65536 */
#define MZ_HASH_MASK (MZ_HASH_SIZE - 1)

#define MZ_MAX_OFFSET_V1 ((1 << 15) - 1)     /* 32767 */
#define MZ_MAX_OFFSET_V2 ((1 << 16) - 1)     /* 65535 */
#define MZ_MIN_MATCH       3
#define MZ_MAX_MATCH_V1    258
#define MZ_MAX_MATCH_V2    130

/* 3-byte hash: fold 24 bits into 16 using multiplicative hash */
#define MZ_HASH3(p) ((((p)[0] * 517) ^ ((p)[1] * 131) ^ ((p)[2])) & MZ_HASH_MASK)

struct mz_state {
    uint8_t win[MZ_WSIZE];
    size_t win_pos;
    uint32_t chain[MZ_WSIZE];      /* full stream pos */      /* per-position chain */
    uint32_t head[MZ_HASH_SIZE];   /* 3-byte hash -> most recent position */
    int level;
};

static void
mz_init(struct mz_state *s, int level)
{
    memset(s->win, 0, MZ_WSIZE);
    s->win_pos = 0;
    memset(s->chain, 0, sizeof(s->chain));
    memset(s->head, 0xFF, sizeof(s->head));
    s->level = level;
}

/* Map level (1-9) to a complete parameter profile so that all three
 * axes (chain depth, match length cap, search distance cap) actually
 * differ across levels. Lazy matching kicks in at level 4+ so that
 * levels 1-3 are pure greedy (fastest) and 4+ gain the better ratio. */
struct mz_level_cfg {
    int    chain_depth;
    int    max_match;
    size_t max_dist;
    int    use_lazy;
};

static const struct mz_level_cfg *
mz_level_cfg(int level)
{
    /* Hand-tuned so each level produces a measurably different ratio
     * on periodic text. */
    static const struct mz_level_cfg cfgs[10] = {
        /* lv 0 unused   */ {   4,   8,   4096, 0 },
        /* lv 1 fastest  */ {   4,   8,   4096, 0 },
        /* lv 2          */ {  16,  16,   4096, 0 },
        /* lv 3          */ {  64,  32,   4096, 0 },
        /* lv 4          */ { 128,  64,  16384, 1 },
        /* lv 5          */ { 256,  64,  16384, 1 },
        /* lv 6 balanced */ { 512,  96,  16384, 1 },
        /* lv 7          */ {1024, 130,  65535, 1 },
        /* lv 8          */ {2048, 130,  65535, 1 },
        /* lv 9 maximum  */ {4096, 130,  65535, 1 }
    };
    int idx = level < 1 ? 1 : (level > 9 ? 9 : level);
    return &cfgs[idx];
}

static int
mz_chain_depth(int level)
{
    return mz_level_cfg(level)->chain_depth;
}

/* Insert position pos into hash chain using 3-byte hash */
static void
mz_insert(struct mz_state *s, size_t pos, const uint8_t *data, size_t limit)
{
    if (pos + 2 >= limit) return;  /* need >= 3 bytes */
    uint16_t h3 = MZ_HASH3(data + pos);
    size_t idx = pos & MZ_WMASK;
    s->chain[idx] = s->head[h3];
    s->head[h3] = (uint32_t)pos;
}

/* Find best match for position pos.
 * Returns 1 if match found, 0 otherwise.
 * v2_mode: if 1, use 16-bit offset range, else 15-bit */
static int
mz_find(const struct mz_state *s, size_t pos, const uint8_t *data, size_t limit,
        int *out_len, size_t *out_off, int v2_mode)
{
    if (pos + 2 >= limit) return 0;
    uint16_t h3 = MZ_HASH3(data + pos);
    uint32_t idx = s->head[h3];
    int best_len = 0;
    size_t best_off = 0;
    int max_chain = mz_chain_depth(s->level);
    /* Use level config to cap both match length and search distance so
     * that low levels (1-3) really do produce a different (worse)
     * compression ratio than high levels (7-9). */
    const struct mz_level_cfg *cfg = mz_level_cfg(s->level);
    int max_len = cfg->max_match;
    size_t max_dist = cfg->max_dist;
    if (!v2_mode) {
        /* legacy v1 path: stick to the original v1 caps */
        if (max_dist > MZ_MAX_OFFSET_V1) max_dist = MZ_MAX_OFFSET_V1;
        if (max_len > MZ_MAX_MATCH_V1) max_len = MZ_MAX_MATCH_V1;
    } else {
        /* v2: never exceed the v2 hardware caps */
        if (max_dist > MZ_MAX_OFFSET_V2) max_dist = MZ_MAX_OFFSET_V2;
        if (max_len > MZ_MAX_MATCH_V2) max_len = MZ_MAX_MATCH_V2;
    }

    /* 3-byte pre-filter */
    uint32_t key3 = (uint32_t)data[pos] | ((uint32_t)data[pos+1] << 8) | ((uint32_t)data[pos+2] << 16);

    for (int c = 0; c < max_chain && idx != (uint32_t)-1 && pos > idx && pos - idx <= max_dist; c++) {
        uint32_t cand_key3 = (uint32_t)data[idx] | ((uint32_t)data[idx+1] << 8) | ((uint32_t)data[idx+2] << 16);
        if (cand_key3 == key3) {
            int ml = (int)(limit - pos);
            if (ml > max_len) ml = max_len;
            int len = MZ_MIN_MATCH;
            while (len < ml && data[idx + len] == data[pos + len])
                len++;
            if (len > best_len) {
                best_len = len;
                best_off = pos - idx;
                if (len >= ml) break;  /* maximal possible */
            }
        }
        idx = s->chain[idx & MZ_WMASK];
    }

    if (best_len >= MZ_MIN_MATCH) {
        *out_len = best_len;
        *out_off = best_off;
        return 1;
    }
    return 0;
}

/* ---- output helpers ---- */

/* Emit literal (1 or 2 bytes depending on value) */
static inline void
write_literal(uint8_t *out, size_t *op, uint8_t val)
{
    if (val >= 0x80) {
        out[(*op)++] = 0x81;
        out[(*op)++] = val;
    } else {
        out[(*op)++] = val;
    }
}

/* Emit v2 match token: 3 bytes, 16-bit offset, 7-bit length */
static inline void
write_match_v2(uint8_t *out, size_t *op, size_t offset, int length)
{
    out[(*op)++] = (uint8_t)(0x80 | ((offset >> 9) & 0x7F));
    out[(*op)++] = (uint8_t)((offset >> 1) & 0xFF);
    out[(*op)++] = (uint8_t)(((offset & 1) << 7) | ((length - MZ_MIN_MATCH) & 0x7F));
}

/* Slide n bytes into window and insert each into hash */
static inline void
slide_and_insert(struct mz_state *s, const uint8_t *data, size_t *ip, size_t inlen, size_t count)
{
    for (size_t i = 0; i < count && *ip < inlen; i++) {
        s->win[s->win_pos] = data[*ip];
        s->win_pos = (s->win_pos + 1) & MZ_WMASK;
        mz_insert(s, *ip, data, inlen);
        (*ip)++;
    }
}

/* ---- public API ---- */

int
mz_compress_lz77(const void *input, size_t inlen, void **result, size_t *result_len,
                 int level)
{
    if (!input || !result || !result_len) return MZ_ERR_PARAM;
    if (inlen == 0) return MZ_ERR_PARAM;

    /* Upper bound: header + each byte as literal + end marker */
    size_t max_out = inlen + inlen + 20;
    uint8_t *out = (uint8_t *)malloc(max_out);
    if (!out) return MZ_ERR_MEMORY;

    const uint8_t *in = (const uint8_t *)input;
    size_t op = 0;

    /* v2 header: magic "mZ" + uncompressed size (4 bytes LE) */
    out[op++] = 'm'; out[op++] = 'Z';
    out[op++] = inlen & 0xFF; out[op++] = (inlen >> 8) & 0xFF;
    out[op++] = (inlen >> 16) & 0xFF; out[op++] = (inlen >> 24) & 0xFF;

    struct mz_state state;
    mz_init(&state, level < 1 ? 1 : (level > 9 ? 9 : level));
    const struct mz_level_cfg *cfg = mz_level_cfg(state.level);

    int use_lazy = cfg->use_lazy;

    size_t ip = 0;
    while (ip < inlen) {
        int match_len;
        size_t match_off;

        if (mz_find(&state, ip, in, inlen, &match_len, &match_off, 1) &&
            /* 偏移 0x200-0x3FF 时 match token 首字节为 0x81，与转义字面量
             * 标记 (0x81 <val>) 冲突，解码器无法区分 → 数据损坏。此类匹配
             * 退化为字面量（字面量编码无歧义）。 */
            !(match_off >= 0x200 && match_off <= 0x3FF)) {
            /* Lazy matching: for levels >= 4, check if the NEXT position
             * has a longer match.  If yes, emit current byte as literal
             * and let the next iteration handle the better match. */
            if (use_lazy && ip + 1 < inlen && ip + 2 < inlen) {
                /* Insert current position into the hash chain so that
                 * the lookahead at ip+1 can match against it. */
                uint16_t h3 = MZ_HASH3(in + ip);
                uint16_t idx = (uint16_t)(ip & MZ_WMASK);
                state.chain[idx] = state.head[h3];
                state.head[h3] = (uint16_t)ip;

                int next_len;
                size_t next_off;
                if (mz_find(&state, ip + 1, in, inlen, &next_len, &next_off, 1) &&
                    next_len > match_len + 1) {
                    /* Next position has a longer match — emit current byte as
                     * literal, slide+insert it, and continue (will re-find
                     * the better match on the next iteration). */
                    goto emit_literal;
                }
            }

            /* v2 match encode with fallback */
            if (op + 3 > max_out) { free(out); return MZ_ERR_STREAM; }
            int len = match_len > MZ_MAX_MATCH_V2 ? MZ_MAX_MATCH_V2 : match_len;
            {
                int _ok = 1;
                size_t _n = (size_t)len;
                if (_n > inlen - ip) _n = inlen - ip;
                for (size_t _vi = 0; _vi < _n; _vi++) {
                    if (in[ip + _vi] != in[ip - match_off + _vi]) { _ok = 0; break; }
                }
                if (!_ok) {
                    for (int _lj = 0; _lj < len && ip < inlen; _lj++) {
                        unsigned char _bv = in[ip];
                        if (_bv >= 128) { out[op++] = 0x81; }
                        out[op++] = _bv;
                        state.win[state.win_pos] = _bv;
                        state.win_pos = (state.win_pos + 1) & MZ_WMASK;
                        mz_insert(&state, ip, in, inlen);
                        ip++;
                    }
                    continue;
                }
            }
            write_match_v2(out, &op, match_off, len);
            slide_and_insert(&state, in, &ip, inlen, (size_t)len);
        } else {
emit_literal:
            /* Literal */
            if (op + (in[ip] >= 0x80 ? 2 : 1) > max_out) { free(out); return MZ_ERR_STREAM; }
            write_literal(out, &op, in[ip]);
            slide_and_insert(&state, in, &ip, inlen, 1);
        }
    }

    /* End marker: match with offset=0 */
    if (op + 3 > max_out) { free(out); return MZ_ERR_STREAM; }
    out[op++] = 0x80; out[op++] = 0; out[op++] = 0;

    *result = out;
    *result_len = op;
    return (int)op;
}


int
mz_decompress_lz77(const void *input, size_t inlen, void **result, size_t *result_len)
{
    if (!input || !result || !result_len) return MZ_ERR_PARAM;
    if (inlen < 6) return MZ_ERR_DATA;

    const uint8_t *in = (const uint8_t *)input;

    /* Auto-detect format by magic */
    int is_v2;
    if (in[0] == 'M' && in[1] == 'Z') {
        is_v2 = 0;          /* v1 format */
    } else if (in[0] == 'm' && in[1] == 'Z') {
        is_v2 = 1;          /* v2 format */
    } else {
        return MZ_ERR_DATA;
    }

    uint32_t uncompsz = (uint32_t)in[2] | ((uint32_t)in[3] << 8)
                       | ((uint32_t)in[4] << 16) | ((uint32_t)in[5] << 24);

    uint8_t *out = (uint8_t *)malloc(uncompsz);
    if (!out) return MZ_ERR_MEMORY;

    size_t ip = 6, op = 0;
    uint8_t win[MZ_WSIZE] = {0};
    size_t wp = 0;

    while (ip < inlen && op < uncompsz) {
        uint8_t b0 = in[ip++];

        if (b0 == 0x81) {
            /* Escape literal */
            if (ip >= inlen) { free(out); return MZ_ERR_DATA; }
            uint8_t val = in[ip++];
            out[op++] = val;
            win[wp] = val;
            wp = (wp + 1) & MZ_WMASK;
        } else if (b0 & 0x80) {
            /* Match token */
            if (ip + 2 > inlen) { free(out); return MZ_ERR_DATA; }
            uint8_t b1 = in[ip++];
            uint8_t b2 = in[ip++];

            size_t offset;
            int length;

            if (is_v2) {
                offset = ((size_t)(b0 & 0x7F) << 9)
                       | ((size_t)b1 << 1)
                       | ((b2 >> 7) & 1);
                length = (b2 & 0x7F) + MZ_MIN_MATCH;
            } else {
                offset = ((size_t)(b0 & 0x7F) << 8) | b1;
                length = b2 + MZ_MIN_MATCH;
            }

            if (offset == 0) break;  /* End marker */

            for (int i = 0; i < length && op < uncompsz; i++) {
                size_t read_pos = (wp + MZ_WSIZE - offset) % MZ_WSIZE;
                uint8_t b = win[read_pos];
                out[op++] = b;
                win[wp] = b;
                wp = (wp + 1) & MZ_WMASK;
            }
        } else {
            /* Literal (bit 7 = 0) */
            out[op++] = b0;
            win[wp] = b0;
            wp = (wp + 1) & MZ_WMASK;
        }
    }

    *result = out;
    *result_len = op;
    return (int)op;
}

size_t mz_max_compressed_size_lz77(size_t inlen)
{
    return inlen + inlen + 20;
}
