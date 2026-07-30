/* mz_ans.c — tANS (table Asymmetric Numeral Systems) compressor/decompressor
 *
 * Format:
 *   Header: 256 frequency values (2 bytes LE each, 512 bytes total)
 *   Data:   tANS bitstream (reverse-stored for correct decode order)
 *
 * Uses 256-state precision with proper state renormalisation.
 * Inspired by Yann Collet's FSE (Finite State Entropy).
 *
 * Bitstream strategy:
 *   Encoder writes bits LSB-first into a byte buffer (forward).
 *   Since tANS encodes symbols BACKWARDS but the decoder reads
 *   FORWARDS, the entire bitstream is byte-reversed at the end.
 *   The decoder reads the reversed bitstream forward (LSB-first),
 *   which restores the correct ANS decode order.
 */

#include "mz.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define ANS_STATE_BITS  8
#define ANS_STATE_COUNT (1 << ANS_STATE_BITS)   /* 256 */

/* ---- bitstream helpers (forward LSB-first) ---- */

struct ans_buf {
    uint8_t *buf;
    size_t cap;
    size_t byte_pos;
    uint64_t hold;
    int hold_bits;
};

static void
ans_buf_init(struct ans_buf *b, uint8_t *buf, size_t cap)
{
    b->buf = buf;
    b->cap = cap;
    b->byte_pos = 0;
    b->hold = 0;
    b->hold_bits = 0;
}

/* Write low n bits to bitstream (LSB first, forward). */
static int
ans_write_bits(struct ans_buf *b, uint64_t value, int n)
{
    b->hold |= (value & ((1ULL << n) - 1)) << b->hold_bits;
    b->hold_bits += n;
    while (b->hold_bits >= 8) {
        if (b->byte_pos >= b->cap) return MZ_ERR_STREAM;
        b->buf[b->byte_pos++] = (uint8_t)(b->hold & 0xFF);
        b->hold >>= 8;
        b->hold_bits -= 8;
    }
    return MZ_OK;
}

/* Flush remaining bits (byte-align). */
static int
ans_flush_bits(struct ans_buf *b)
{
    if (b->hold_bits > 0) {
        if (b->byte_pos >= b->cap) return MZ_ERR_STREAM;
        b->buf[b->byte_pos++] = (uint8_t)(b->hold & 0xFF);
        b->hold = 0;
        b->hold_bits = 0;
    }
    return MZ_OK;
}

/* Read one bit from bitstream (forward). */
static int
ans_read_bit(struct ans_buf *b)
{
    if (b->hold_bits == 0) {
        if (b->byte_pos >= b->cap) return -1;
        b->hold = b->buf[b->byte_pos++];
        b->hold_bits = 8;
    }
    int bit = (int)(b->hold & 1);
    b->hold >>= 1;
    b->hold_bits--;
    return bit;
}

/* Read n bits. Returns -1 on EOF. */
static int
ans_read_bits_val(struct ans_buf *b, int n)
{
    int val = 0;
    for (int i = 0; i < n; i++) {
        int bit = ans_read_bit(b);
        if (bit < 0) return -1;
        val |= bit << i;
    }
    return val;
}

/* ---- frequency normalisation ---- */

static int
normalise_freqs(const unsigned raw[256], int norm[256])
{
    uint64_t total = 0;
    int used = 0;
    for (int i = 0; i < 256; i++) {
        total += raw[i];
        if (raw[i] > 0) used++;
    }
    if (total == 0) return MZ_ERR_DATA;

    if (used == 1) {
        memset(norm, 0, 256 * sizeof(int));
        for (int i = 0; i < 256; i++) {
            if (raw[i] > 0) { norm[i] = ANS_STATE_COUNT; break; }
        }
        return MZ_OK;
    }

    uint64_t target = ANS_STATE_COUNT;
    int sum = 0;
    for (int i = 0; i < 256; i++) {
        if (raw[i] == 0) {
            norm[i] = 0;
        } else {
            uint64_t v = (raw[i] * target + total / 2) / total;
            if (v == 0) v = 1;
            norm[i] = (int)v;
            sum += norm[i];
        }
    }

    int diff = (int)target - sum;
    if (diff > 0) {
        for (int i = 0; i < 256 && diff > 0; i++) {
            if (norm[i] > 0) { norm[i]++; diff--; }
        }
    } else if (diff < 0) {
        for (int i = 255; i >= 0 && diff < 0; i--) {
            if (norm[i] > 1) { norm[i]--; diff++; }
        }
    }
    return MZ_OK;
}

/* floor(log2(x)) for x > 0 */
static int
flog2(uint32_t x)
{
    int n = 0;
    while (x > 1) { x >>= 1; n++; }
    return n;
}

/* ---- table construction ---- */

struct ans_tables {
    int symbol[ANS_STATE_COUNT];
    int nb_bits[ANS_STATE_COUNT];
    int new_state[ANS_STATE_COUNT];
    int encode_next[ANS_STATE_COUNT][256];
    int freq[256];
};

static int
build_tables(struct ans_tables *t, const int norm[256])
{
    int i, s;

    memcpy(t->freq, norm, 256 * sizeof(int));

    /* ---- spread symbols across the state table ---- */
    memset(t->symbol, 0xFF, sizeof(t->symbol));

    int step = (ANS_STATE_COUNT >> 1) + (ANS_STATE_COUNT >> 3) + 3;
    step |= 1; /* ensure odd — coprime with power-of-2 */

    int pos = 0;
    for (s = 0; s < 256; s++) {
        int f = norm[s];
        if (f == 0) continue;
        for (i = 0; i < f; i++) {
            while (t->symbol[pos] >= 0)
                pos = (pos + 1) & (ANS_STATE_COUNT - 1);
            t->symbol[pos] = s;
            pos = (pos + step) & (ANS_STATE_COUNT - 1);
        }
    }

    /* ---- count ranks per symbol ---- */
    int rank[256];
    memset(rank, 0, sizeof(rank));

    /* ---- build decoding tables ---- */
    for (int x = 0; x < ANS_STATE_COUNT; x++) {
        s = t->symbol[x];
        if (s < 0) return MZ_ERR_DATA;
        int f = norm[s];
        int slot = rank[s]++;
        int nb;
        if (f >= ANS_STATE_COUNT) {
            nb = 0;
        } else {
            nb = ANS_STATE_BITS - flog2(f);
            while ((f << nb) < ANS_STATE_COUNT) nb++;
        }
        t->nb_bits[x] = nb;
        t->new_state[x] = slot << nb;
    }

    /* ---- build encoding table ---- */
    for (s = 0; s < 256; s++)
        for (int x = 0; x < ANS_STATE_COUNT; x++)
            t->encode_next[x][s] = -1;

    for (int x = 0; x < ANS_STATE_COUNT; x++) {
        s = t->symbol[x];
        int nb = t->nb_bits[x];
        int base = t->new_state[x];
        int slot = base >> nb;
        if (slot >= norm[s]) continue;
        for (int bits = 0; bits < (1 << nb); bits++) {
            int target = base + bits;
            if (target < ANS_STATE_COUNT) {
                t->encode_next[target][s] = x;
            }
        }
    }

    return MZ_OK;
}

/* ---- compressor ---- */

int
mz_tans_compress(const unsigned char *in, size_t inlen,
                 unsigned char *out, size_t *outlen)
{
    if (!in || !out || !outlen) return MZ_ERR_PARAM;
    if (inlen == 0) return MZ_ERR_PARAM;

    /* Count frequencies */
    unsigned freqs[256];
    memset(freqs, 0, sizeof(freqs));
    for (size_t i = 0; i < inlen; i++)
        freqs[in[i]]++;

    /* Normalise */
    int norm[256];
    int ret = normalise_freqs(freqs, norm);
    if (ret != MZ_OK) return ret;

    /* Build tables */
    struct ans_tables *tab = (struct ans_tables *)malloc(sizeof(*tab));
    if (!tab) return MZ_ERR_MEMORY;
    ret = build_tables(tab, norm);
    if (ret != MZ_OK) { free(tab); return ret; }

    /* Output header: 256 * 2 bytes LE */
    size_t ip = 0;
    size_t max_out = *outlen;
    if (ip + 512 > max_out) { free(tab); return MZ_ERR_STREAM; }
    for (int i = 0; i < 256; i++) {
        out[ip++] = (uint8_t)(norm[i] & 0xFF);
        out[ip++] = (uint8_t)((norm[i] >> 8) & 0xFF);
    }

    /* Reserve space for bitstream (use remaining output buffer) */
    size_t bitstream_max = max_out - ip;
    if (bitstream_max < ANS_STATE_BITS / 8 + 1) { free(tab); return MZ_ERR_STREAM; }

    struct ans_buf bw;
    ans_buf_init(&bw, out + ip, bitstream_max);

    /* Encode: process symbols in REVERSE */
    uint64_t state = ANS_STATE_COUNT / 2;

    size_t idx = inlen;
    while (idx > 0) {
        idx--;
        int sym = in[idx];
        int f = tab->freq[sym];
        if (f == 0) { free(tab); return MZ_ERR_DATA; }
        int nb = ANS_STATE_BITS - flog2(f);
        while ((f << nb) < ANS_STATE_COUNT) nb++;

        /* Renormalise: state must be < min(freq << nb, ANS_STATE_COUNT)
         * for the table lookup to succeed. */
        while (state >= ANS_STATE_COUNT) {
            ret = ans_write_bits(&bw, state & 1, 1);
            if (ret != MZ_OK) { free(tab); return ret; }
            state >>= 1;
        }
        /* Also ensure state < freq << nb for valid decomp */
        uint64_t threshold = (uint64_t)f << nb;
        while (state >= threshold) {
            ret = ans_write_bits(&bw, state & 1, 1);
            if (ret != MZ_OK) { free(tab); return ret; }
            state >>= 1;
        }

        uint64_t slot = state >> nb;
        uint64_t bits = state & ((1ULL << nb) - 1);

        if (slot >= (uint64_t)tab->freq[sym])
            slot = tab->freq[sym] - 1;
        int next = tab->encode_next[(int)state][sym];
        if (next < 0)
            next = (int)(state & (ANS_STATE_COUNT - 1));
        state = (uint64_t)next;

        ret = ans_write_bits(&bw, bits, nb);
        if (ret != MZ_OK) { free(tab); return ret; }
    }

    /* Flush final state (written first in reverse-order bitstream) */
    for (int i = 0; i < ANS_STATE_BITS; i++) {
        ret = ans_write_bits(&bw, state & 1, 1);
        if (ret != MZ_OK) { free(tab); return ret; }
        state >>= 1;
    }

    ret = ans_flush_bits(&bw);
    if (ret != MZ_OK) { free(tab); return ret; }

    /* Reverse the bitstream bytes so that the decoder can read forward */
    size_t bs_len = bw.byte_pos;
    uint8_t *bs = out + ip;
    for (size_t i = 0; i < bs_len / 2; i++) {
        uint8_t tmp = bs[i];
        bs[i] = bs[bs_len - 1 - i];
        bs[bs_len - 1 - i] = tmp;
    }

    *outlen = ip + bs_len;
    free(tab);
    return MZ_OK;
}

/* ---- decompressor ---- */

int
mz_tans_decompress(const unsigned char *in, size_t inlen,
                   unsigned char *out, size_t *outlen)
{
    if (!in || !out || !outlen) return MZ_ERR_PARAM;
    if (inlen < 512) return MZ_ERR_DATA;

    /* Read header */
    size_t ip = 0;
    int norm[256];
    for (int i = 0; i < 256; i++) {
        norm[i] = (int)in[ip] | ((int)in[ip + 1] << 8);
        ip += 2;
    }

    /* Build tables */
    struct ans_tables *tab = (struct ans_tables *)malloc(sizeof(*tab));
    if (!tab) return MZ_ERR_MEMORY;
    int ret = build_tables(tab, norm);
    if (ret != MZ_OK) { free(tab); return ret; }

    /* The bitstream was byte-reversed by the encoder.
     * We read it normally (forward). */
    struct ans_buf br;
    ans_buf_init(&br, (uint8_t *)in + ip, inlen - ip);

    /* Read initial state (was flushed LAST by encoder, now at START of bitstream) */
    uint64_t state = 0;
    for (int i = 0; i < ANS_STATE_BITS; i++) {
        int bit = ans_read_bit(&br);
        if (bit < 0) { free(tab); return MZ_ERR_DATA; }
        state |= (uint64_t)bit << i;
    }

    /* Decode into temp buffer (backwards), then copy */
    uint8_t *tmp = (uint8_t *)malloc(*outlen);
    if (!tmp) { free(tab); return MZ_ERR_MEMORY; }

    size_t op = *outlen;
    while (op > 0) {
        if (state >= ANS_STATE_COUNT) { free(tab); free(tmp); return MZ_ERR_DATA; }
        int x = (int)state;
        int sym = tab->symbol[x];
        if (sym < 0) { free(tab); free(tmp); return MZ_ERR_DATA; }

        op--;
        tmp[op] = (uint8_t)sym;

        int nb = tab->nb_bits[x];
        int base = tab->new_state[x];

        int bits = ans_read_bits_val(&br, nb);
        if (bits < 0) { free(tab); free(tmp); return MZ_ERR_DATA; }

        state = (uint64_t)(base + bits);
        while (state >= ANS_STATE_COUNT) {
            int bit = ans_read_bit(&br);
            if (bit < 0) { free(tab); free(tmp); return MZ_ERR_DATA; }
            state = (state << 1) | (uint64_t)(bit & 1);
        }
    }

    memcpy(out, tmp, *outlen);
    free(tmp);
    free(tab);
    return MZ_OK;
}
