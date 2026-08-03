/* mz_deflate.c — RFC 1951 DEFLATE codec
 *
 * Compress: stored blocks only (BTYPE=00). Each block has a 5-byte header
 *   (BFINAL + BTYPE + LEN + NLEN) followed by raw data. Max block = 65535.
 *
 * Decompress: handles all three block types:
 *   - BTYPE=00: stored (uncompressed)
 *   - BTYPE=01: fixed Huffman codes
 *   - BTYPE=10: dynamic Huffman codes
 *   - BTYPE=11: reserved (error)
 *
 * Bit order: LSB-first within each byte (per RFC 1951 §3.1.1).
 */
#include "mz.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---- bit reader (LSB first) ---- */
struct mz_bit_reader {
    const uint8_t *data;
    size_t len;
    size_t byte_pos;
    int bit_pos;   /* 0..7, LSB first */
};

static void
br_init(struct mz_bit_reader *br, const uint8_t *data, size_t len)
{
    br->data = data;
    br->len = len;
    br->byte_pos = 0;
    br->bit_pos = 0;
}

static int
br_bit(struct mz_bit_reader *br)
{
    if (br->byte_pos >= br->len) return -1;
    int bit = (br->data[br->byte_pos] >> br->bit_pos) & 1;
    br->bit_pos++;
    if (br->bit_pos >= 8) {
        br->bit_pos = 0;
        br->byte_pos++;
    }
    return bit;
}

static unsigned
br_bits(struct mz_bit_reader *br, int n)
{
    unsigned val = 0;
    for (int i = 0; i < n; i++) {
        int b = br_bit(br);
        if (b < 0) return 0;
        val |= (unsigned)b << i;
    }
    return val;
}

static void
br_align(struct mz_bit_reader *br)
{
    if (br->bit_pos > 0) {
        br->bit_pos = 0;
        br->byte_pos++;
    }
}

/* ---- Huffman decode ---- */
#define MZ_MAX_HUFF_BITS 15
#define MZ_MAX_HUFF_SYMBOLS 288

struct mz_huff_table {
    int counts[MZ_MAX_HUFF_BITS + 1];  /* number of codes of each length */
    int symbols[MZ_MAX_HUFF_SYMBOLS];  /* symbols sorted by code length */
};

static int
huff_build(struct mz_huff_table *ht, const uint8_t *lengths, int num_symbols)
{
    memset(ht->counts, 0, sizeof(ht->counts));
    for (int i = 0; i < num_symbols; i++) {
        if (lengths[i] > MZ_MAX_HUFF_BITS) return -1;
        ht->counts[lengths[i]]++;
    }
    ht->counts[0] = 0;  /* don't count zero-length */

    /* Check for over-subscribed code */
    int left = 1;
    for (int len = 1; len <= MZ_MAX_HUFF_BITS; len++) {
        left <<= 1;
        left -= ht->counts[len];
        if (left < 0) return -1;
    }

    /* Build offsets for each length */
    int offsets[MZ_MAX_HUFF_BITS + 1] = {0};
    int off = 0;
    for (int len = 1; len <= MZ_MAX_HUFF_BITS; len++) {
        offsets[len] = off;
        off += ht->counts[len];
    }

    /* Place symbols sorted by code length */
    for (int sym = 0; sym < num_symbols; sym++) {
        if (lengths[sym] > 0)
            ht->symbols[offsets[lengths[sym]]++] = sym;
    }

    return left;  /* 0 = complete, >0 = under-subscribed */
}

static int
huff_decode(struct mz_bit_reader *br, const struct mz_huff_table *ht)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MZ_MAX_HUFF_BITS; len++) {
        int b = br_bit(br);
        if (b < 0) return -1;
        code |= b;
        int count = ht->counts[len];
        if (code - first < count)
            return ht->symbols[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;  /* invalid code */
}

/* ---- length/distance tables (RFC 1951 §3.2.5) ---- */

static const uint8_t s_length_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const uint8_t s_length_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const uint16_t s_dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,
    4097,6145,8193,12289,16385,24577
};
static const uint8_t s_dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

/* Code order for dynamic Huffman (RFC 1951 §3.2.7) */
static const uint8_t s_code_order[19] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
};

/* ---- decompress ---- */

static int
inflate_block(struct mz_bit_reader *br, uint8_t **out, size_t *out_pos, size_t *out_cap)
{
    int bfinal = br_bit(br);
    if (bfinal < 0) return MZ_ERR_DATA;
    int btype = br_bits(br, 2);

    if (btype == 0) {
        /* Stored block */
        br_align(br);
        if (br->byte_pos + 4 > br->len) return MZ_ERR_DATA;
        uint16_t len = br->data[br->byte_pos] | (br->data[br->byte_pos + 1] << 8);
        uint16_t nlen = br->data[br->byte_pos + 2] | (br->data[br->byte_pos + 3] << 8);
        br->byte_pos += 4;
        if ((uint16_t)~len != nlen) return MZ_ERR_DATA;

        if (*out_pos + len > *out_cap) {
            size_t new_cap = (*out_cap + len) * 2;
            uint8_t *p = realloc(*out, new_cap);
            if (!p) return MZ_ERR_MEMORY;
            *out = p;
            *out_cap = new_cap;
        }
        if (br->byte_pos + len > br->len) return MZ_ERR_DATA;
        memcpy(*out + *out_pos, br->data + br->byte_pos, len);
        br->byte_pos += len;
        *out_pos += len;
        return bfinal ? 1 : 0;
    }

    struct mz_huff_table lit_ht, dist_ht;

    if (btype == 1) {
        /* Fixed Huffman */
        uint8_t lit_lengths[288];
        for (int i = 0; i <= 143; i++) lit_lengths[i] = 8;
        for (int i = 144; i <= 255; i++) lit_lengths[i] = 9;
        for (int i = 256; i <= 279; i++) lit_lengths[i] = 7;
        for (int i = 280; i <= 287; i++) lit_lengths[i] = 8;
        huff_build(&lit_ht, lit_lengths, 288);

        uint8_t dist_lengths[30];
        memset(dist_lengths, 5, 30);
        huff_build(&dist_ht, dist_lengths, 30);
    } else if (btype == 2) {
        /* Dynamic Huffman */
        int hlit = br_bits(br, 5) + 257;
        int hdist = br_bits(br, 5) + 1;
        int hclen = br_bits(br, 4) + 4;

        uint8_t code_lengths[19] = {0};
        for (int i = 0; i < hclen; i++)
            code_lengths[s_code_order[i]] = br_bits(br, 3);

        struct mz_huff_table code_ht;
        if (huff_build(&code_ht, code_lengths, 19) < 0) return MZ_ERR_DATA;

        uint8_t lit_lengths[288] = {0};
        uint8_t dist_lengths[30] = {0};
        int total = hlit + hdist;
        int pos = 0;
        while (pos < total) {
            int sym = huff_decode(br, &code_ht);
            if (sym < 0) return MZ_ERR_DATA;
            if (sym < 16) {
                if (pos < hlit)
                    lit_lengths[pos] = sym;
                else
                    dist_lengths[pos - hlit] = sym;
                pos++;
            } else if (sym == 16) {
                if (pos == 0) return MZ_ERR_DATA;
                int rep = br_bits(br, 2) + 3;
                uint8_t prev = (pos - 1 < hlit) ? lit_lengths[pos - 1] : dist_lengths[pos - 1 - hlit];
                while (rep-- > 0 && pos < total) {
                    if (pos < hlit)
                        lit_lengths[pos] = prev;
                    else
                        dist_lengths[pos - hlit] = prev;
                    pos++;
                }
            } else if (sym == 17) {
                int rep = br_bits(br, 3) + 3;
                pos += rep;
                if (pos > total) return MZ_ERR_DATA;
            } else if (sym == 18) {
                int rep = br_bits(br, 7) + 11;
                pos += rep;
                if (pos > total) return MZ_ERR_DATA;
            }
        }
        if (huff_build(&lit_ht, lit_lengths, hlit) < 0) return MZ_ERR_DATA;
        if (huff_build(&dist_ht, dist_lengths, hdist) < 0) return MZ_ERR_DATA;
    } else {
        return MZ_ERR_DATA;
    }

    /* Decode symbols */
    for (;;) {
        int sym = huff_decode(br, &lit_ht);
        if (sym < 0) return MZ_ERR_DATA;
        if (sym < 256) {
            /* Literal */
            if (*out_pos >= *out_cap) {
                size_t new_cap = *out_cap * 2 + 1024;
                uint8_t *p = realloc(*out, new_cap);
                if (!p) return MZ_ERR_MEMORY;
                *out = p;
                *out_cap = new_cap;
            }
            (*out)[(*out_pos)++] = sym;
        } else if (sym == 256) {
            /* End of block */
            return bfinal ? 1 : 0;
        } else {
            /* Length/distance */
            int li = sym - 257;
            if (li >= 29) return MZ_ERR_DATA;
            int length = s_length_base[li] + br_bits(br, s_length_extra[li]);

            int dsym = huff_decode(br, &dist_ht);
            if (dsym < 0 || dsym >= 30) return MZ_ERR_DATA;
            int dist = s_dist_base[dsym] + br_bits(br, s_dist_extra[dsym]);

            if ((size_t)dist > *out_pos) return MZ_ERR_DATA;

            if (*out_pos + length > *out_cap) {
                size_t new_cap = (*out_cap + length) * 2;
                uint8_t *p = realloc(*out, new_cap);
                if (!p) return MZ_ERR_MEMORY;
                *out = p;
                *out_cap = new_cap;
            }
            for (int i = 0; i < length; i++) {
                (*out)[*out_pos] = (*out)[*out_pos - dist];
                (*out_pos)++;
            }
        }
    }
}

int
mz_deflate_compress(const void *input, size_t inlen, void **result, size_t *result_len)
{
    if (!input || !result || !result_len) return MZ_ERR_PARAM;

    /* Stored blocks: each block = 5 header + up to 65535 data */
    size_t num_blocks = (inlen + 65534) / 65535;
    size_t max_out = inlen + num_blocks * 5 + 1;
    uint8_t *out = malloc(max_out);
    if (!out) return MZ_ERR_MEMORY;

    const uint8_t *in = input;
    size_t op = 0;
    size_t ip = 0;

    while (ip < inlen) {
        size_t block_len = inlen - ip;
        if (block_len > 65535) block_len = 65535;
        int is_final = (ip + block_len >= inlen);

        /* BFINAL + BTYPE=00 (stored) */
        out[op++] = is_final ? 0x01 : 0x00;
        /* LEN (16-bit LE) */
        out[op++] = block_len & 0xFF;
        out[op++] = (block_len >> 8) & 0xFF;
        /* NLEN = ~LEN (16-bit LE) */
        uint16_t nlen = ~block_len;
        out[op++] = nlen & 0xFF;
        out[op++] = (nlen >> 8) & 0xFF;
        /* Data */
        memcpy(out + op, in + ip, block_len);
        op += block_len;
        ip += block_len;
    }

    if (inlen == 0) {
        /* Empty input: single empty final block */
        out[op++] = 0x01;  /* BFINAL=1, BTYPE=00 */
        out[op++] = 0x00;
        out[op++] = 0x00;
        out[op++] = 0xFF;
        out[op++] = 0xFF;
    }

    *result = out;
    *result_len = op;
    return (int)op;
}

int
mz_deflate_decompress(const void *input, size_t inlen, void **result, size_t *result_len)
{
    if (!input || !result || !result_len) return MZ_ERR_PARAM;
    if (inlen == 0) return MZ_ERR_DATA;

    struct mz_bit_reader br;
    br_init(&br, input, inlen);

    size_t out_cap = inlen * 4 + 1024;
    uint8_t *out = malloc(out_cap);
    if (!out) return MZ_ERR_MEMORY;
    size_t out_pos = 0;

    int done = 0;
    while (!done) {
        int rc = inflate_block(&br, &out, &out_pos, &out_cap);
        if (rc < 0) {
            free(out);
            return rc;
        }
        done = rc;
    }

    *result = out;
    *result_len = out_pos;
    return (int)out_pos;
}
