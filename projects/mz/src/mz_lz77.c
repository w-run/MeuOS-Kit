#include "mz.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* MZ LZ77 — Minimal LZ77 compressor/decompressor
 * Format: "MZ" magic + 4-byte uncompressed size + stream of tokens
 * Token: (1 bit type) + (7 bit data) or (15 bit match)
 *   0xxxxxxx = literal byte
 *   1ooooooo ol llllllll = match: 15-bit offset, 8-bit length+3
 *   (big-endian bit layout: bit 15 is match flag, bits 14-0 vary)
 *
 * This simple format is designed for correctness first.
 */

#define MZ_WBITS 12
#define MZ_WSIZE (1 << MZ_WBITS)
#define MZ_WMASK (MZ_WSIZE - 1)

#define MZ_MAX_OFFSET ((1 << 15) - 1)
#define MZ_MIN_MATCH 3
#define MZ_MAX_MATCH 258

struct mz_state {
    uint8_t win[MZ_WSIZE];
    size_t win_pos;
    uint16_t chain[MZ_WSIZE];
    uint16_t head[1 << 16]; /* direct-mapped: use 2-byte hash as key */
};

static void
mz_init(struct mz_state *s)
{
    memset(s->win, 0, MZ_WSIZE);
    s->win_pos = 0;
    memset(s->chain, 0, sizeof(s->chain));
    memset(s->head, 0xFF, sizeof(s->head));
}

static void
mz_insert(struct mz_state *s, size_t pos, const uint8_t *data, size_t limit)
{
    if (pos + 1 >= limit) return;
    uint32_t key = data[pos] | ((uint32_t)data[pos+1] << 8);
    uint16_t idx = (uint16_t)(pos & MZ_WMASK);
    s->chain[idx] = s->head[key];
    s->head[key] = (uint16_t)pos;
}

static int
mz_find(const struct mz_state *s, size_t pos, const uint8_t *data, size_t limit,
        int *out_len, size_t *out_off)
{
    if (pos + 1 >= limit) return 0;
    uint32_t key = data[pos] | ((uint32_t)data[pos+1] << 8);
    uint16_t idx = s->head[key];
    int best_len = 0;
    size_t best_off = 0;
    int max_chain = 256;

    for (int c = 0; c < max_chain && idx != 0xFFFF && pos > idx && pos - idx <= MZ_WSIZE; c++) {
        int ml = (int)(limit - pos);
        if (ml > MZ_MAX_MATCH) ml = MZ_MAX_MATCH;
        int len = 0;
        while (len < ml && data[idx + len] == data[pos + len])
            len++;
        if (len >= MZ_MIN_MATCH && len > best_len) {
            best_len = len;
            best_off = pos - idx;
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

int
mz_compress(const void *input, size_t inlen, void **result, size_t *result_len,
             int codec, int level)
{
    if (!input || !result || !result_len || codec != 1) return MZ_ERR_PARAM;
    (void)level;

    /* Upper bound: worst case is each byte stored as literal + header + end marker */
    size_t max_out = inlen + inlen + 20;
    uint8_t *out = (uint8_t *)malloc(max_out);
    if (!out) return MZ_ERR_MEMORY;

    const uint8_t *in = (const uint8_t *)input;
    size_t op = 0;

    /* Header: magic "MZ" + uncompressed size (4 bytes LE) */
    out[op++] = 'M'; out[op++] = 'Z';
    out[op++] = inlen & 0xFF; out[op++] = (inlen >> 8) & 0xFF;
    out[op++] = (inlen >> 16) & 0xFF; out[op++] = (inlen >> 24) & 0xFF;

    struct mz_state state;
    mz_init(&state);

    size_t ip = 0;
    while (ip < inlen) {
        int match_len;
        size_t match_off;

        if (mz_find(&state, ip, in, inlen, &match_len, &match_off)) {
            /* Encode match: 2 bytes — 1 bit flag + 15 bits offset, then 1 byte length */
            /* Byte 0: 1ooooooo (flag + offset high 7 bits) */
            /* Byte 1: oooooooo (offset low 8 bits) */
            /* Byte 2: llllllll (length - 3, 8 bits = 3..258) */
            if (op + 3 > max_out) { free(out); return MZ_ERR_STREAM; }
            if (match_off > MZ_MAX_OFFSET) match_off = MZ_MAX_OFFSET;
            int len = match_len > MZ_MAX_MATCH ? MZ_MAX_MATCH : match_len;
            out[op++] = (uint8_t)(0x80 | ((match_off >> 8) & 0x7F));
            out[op++] = (uint8_t)(match_off & 0xFF);
            out[op++] = (uint8_t)(len - MZ_MIN_MATCH);

            for (int i = 0; i < len && ip < inlen; i++) {
                state.win[state.win_pos] = in[ip];
                state.win_pos = (state.win_pos + 1) & MZ_WMASK;
                mz_insert(&state, ip, in, inlen);
                ip++;
            }
        } else {
            /* Encode literal: 1 byte (bit 7 = 0) */
            if (op + 1 > max_out) { free(out); return MZ_ERR_STREAM; }
            out[op++] = in[ip] & 0x7F;  /* literal, bit 7 = 0 */

            state.win[state.win_pos] = in[ip];
            state.win_pos = (state.win_pos + 1) & MZ_WMASK;
            mz_insert(&state, ip, in, inlen);
            ip++;
        }
    }

    /* End marker: match with offset=0 (invalid, signals end) */
    if (op + 3 > max_out) { free(out); return MZ_ERR_STREAM; }
    out[op++] = 0x80; out[op++] = 0; out[op++] = 0;

    *result = out;
    *result_len = op;
    return (int)op;
}

int
mz_decompress(const void *input, size_t inlen, void **result, size_t *result_len,
               int codec)
{
    if (!input || !result || !result_len || codec != 1) return MZ_ERR_PARAM;
    if (inlen < 6) return MZ_ERR_DATA;

    const uint8_t *in = (const uint8_t *)input;
    if (in[0] != 'M' || in[1] != 'Z') return MZ_ERR_DATA;

    uint32_t uncompsz = (uint32_t)in[2] | ((uint32_t)in[3] << 8)
                       | ((uint32_t)in[4] << 16) | ((uint32_t)in[5] << 24);

    uint8_t *out = (uint8_t *)malloc(uncompsz);
    if (!out) return MZ_ERR_MEMORY;

    size_t ip = 6, op = 0;
    uint8_t win[MZ_WSIZE] = {0};
    size_t wp = 0;

    while (ip < inlen && op < uncompsz) {
        uint8_t b0 = in[ip++];

        if (b0 & 0x80) {
            /* Match */
            if (ip + 2 > inlen) { free(out); return MZ_ERR_DATA; }
            uint8_t b1 = in[ip++];
            uint8_t b2 = in[ip++];

            size_t offset = ((size_t)(b0 & 0x7F) << 8) | b1;
            int length = b2 + MZ_MIN_MATCH;

            if (offset == 0) break;  /* End marker */

            for (int i = 0; i < length && op < uncompsz; i++) {
                size_t src = (wp + MZ_WSIZE - offset) % MZ_WSIZE;
                src = (src + i) % MZ_WSIZE;
                /* Handle case where offset < i (RLE-like, overlapping match) */
                size_t read_pos = (wp + MZ_WSIZE - offset) % MZ_WSIZE;
                read_pos = (read_pos + i) % MZ_WSIZE;
                uint8_t b = win[read_pos];
                out[op++] = b;
                win[wp] = b;
                wp = (wp + 1) & MZ_WMASK;
            }
        } else {
            /* Literal */
            out[op++] = b0;
            win[wp] = b0;
            wp = (wp + 1) & MZ_WMASK;
        }
    }

    *result = out;
    *result_len = op;
    return (int)op;
}

size_t mz_max_compressed_size(size_t inlen, int codec) {
    (void)codec; return inlen + inlen + 20;
}
