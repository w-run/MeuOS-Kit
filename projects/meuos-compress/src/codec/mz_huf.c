#include "mz.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* MZ Huffman — static Huffman compressor/decompressor
 *
 * Format:
 *   Header: symbol_count(2B LE) + code_len[256](1B each, 0 = unused)
 *   Data:   canonical Huffman bitstream (MSB first per byte)
 *   Padding: last byte's unused bits are zero
 *
 * Canonical code assignment:
 *   1) Sort symbols by (code_len, symbol)
 *   2) first_code = 0; for each len: first_code <<= (len - prev_len)
 *   3) Assign codes in sorted order, incrementing first_code
 */

#define MAX_SYMBOLS 256
#define MAX_CODE_LEN 32

/* ---- Min-heap for Huffman tree building ---- */

struct huf_heap {
    unsigned freq;
    int node_idx;
};

static void
heap_sift_up(struct huf_heap *heap, int i)
{
    while (i > 0) {
        int p = (i - 1) / 2;
        if (heap[p].freq <= heap[i].freq) break;
        struct huf_heap t = heap[p]; heap[p] = heap[i]; heap[i] = t;
        i = p;
    }
}

static void
heap_sift_down(struct huf_heap *heap, int n)
{
    int i = 0;
    for (;;) {
        int s = i, l = 2 * i + 1, r = 2 * i + 2;
        if (l < n && heap[l].freq < heap[s].freq) s = l;
        if (r < n && heap[r].freq < heap[s].freq) s = r;
        if (s == i) break;
        struct huf_heap t = heap[i]; heap[i] = heap[s]; heap[s] = t;
        i = s;
    }
}

static inline void
heap_push(struct huf_heap *heap, int *n, unsigned freq, int node_idx)
{
    heap[*n].freq = freq;
    heap[*n].node_idx = node_idx;
    heap_sift_up(heap, (*n)++);
}

static inline struct huf_heap
heap_pop(struct huf_heap *heap, int *n)
{
    struct huf_heap min = heap[0];
    heap[0] = heap[--(*n)];
    heap_sift_down(heap, *n);
    return min;
}

/* ---- Huffman tree ---- */

struct huf_node {
    int sym;         /* -1 for internal */
    unsigned freq;
    int left, right; /* child indices, -1 = nil */
};

static void
calc_lengths(const struct huf_node *nodes, int idx, int depth,
             int *lengths)
{
    if (nodes[idx].sym >= 0) {
        lengths[nodes[idx].sym] = depth > 0 ? depth : 1;
        return;
    }
    if (nodes[idx].left >= 0)
        calc_lengths(nodes, nodes[idx].left, depth + 1, lengths);
    if (nodes[idx].right >= 0)
        calc_lengths(nodes, nodes[idx].right, depth + 1, lengths);
}

static int
build_tree(struct huf_node *nodes, int *node_count,
           const unsigned freqs[MAX_SYMBOLS])
{
    struct huf_heap heap[512];
    int heap_n = 0;
    int ncount = 0;

    for (int i = 0; i < MAX_SYMBOLS; i++) {
        if (freqs[i] > 0) {
            nodes[ncount].sym = i;
            nodes[ncount].freq = freqs[i];
            nodes[ncount].left = -1;
            nodes[ncount].right = -1;
            heap_push(heap, &heap_n, freqs[i], ncount);
            ncount++;
        }
    }

    if (heap_n == 0) return 0;
    if (heap_n == 1) {
        nodes[ncount].sym = -1;
        nodes[ncount].freq = freqs[nodes[0].sym];
        nodes[ncount].left = 0;
        nodes[ncount].right = -1;
        ncount++;
        *node_count = ncount;
        return 1;
    }

    while (heap_n > 1) {
        struct huf_heap a = heap_pop(heap, &heap_n);
        struct huf_heap b = heap_pop(heap, &heap_n);
        nodes[ncount].sym = -1;
        nodes[ncount].freq = a.freq + b.freq;
        nodes[ncount].left = a.node_idx;
        nodes[ncount].right = b.node_idx;
        heap_push(heap, &heap_n, nodes[ncount].freq, ncount);
        ncount++;
    }

    *node_count = ncount;
    return 1;
}

/* ---- Canonical code generation ---- */

struct sym_len {
    int sym;
    int len;
};

static int
sl_cmp(const void *a, const void *b)
{
    const struct sym_len *sa = (const struct sym_len *)a;
    const struct sym_len *sb = (const struct sym_len *)b;
    if (sa->len != sb->len) return sa->len - sb->len;
    return sa->sym - sb->sym;
}

static void
make_canonical(unsigned *codes, int *code_lens, const int *lengths,
               int max_len)
{
    (void)max_len;
    struct sym_len sorted[MAX_SYMBOLS];
    int n = 0;
    for (int i = 0; i < MAX_SYMBOLS; i++) {
        if (lengths[i] > 0) {
            sorted[n].sym = i;
            sorted[n].len = lengths[i];
            n++;
        }
    }
    qsort(sorted, (size_t)n, sizeof(struct sym_len), sl_cmp);

    unsigned code = 0;
    int prev_len = 0;
    for (int i = 0; i < n; i++) {
        int sym = sorted[i].sym;
        int len = sorted[i].len;
        code <<= (len - prev_len);
        codes[sym] = code;
        code_lens[sym] = len;
        code++;
        prev_len = len;
    }
}

/* ---- Bitstream writer (MSB first) ---- */

struct bit_writer {
    uint8_t *buf;
    size_t cap;
    size_t byte_pos;
    int bit_pos;  /* 0..7 */
};

static void
bw_init(struct bit_writer *bw, uint8_t *buf, size_t cap)
{
    bw->buf = buf;
    bw->cap = cap;
    bw->byte_pos = 0;
    bw->bit_pos = 0;
    if (cap > 0) buf[0] = 0;
}

static int
bw_write_bits(struct bit_writer *bw, unsigned code, int nbits)
{
    for (int i = nbits - 1; i >= 0; i--) {
        if (bw->byte_pos >= bw->cap) return MZ_ERR_STREAM;
        bw->buf[bw->byte_pos] |= ((code >> i) & 1) << (7 - bw->bit_pos);
        bw->bit_pos++;
        if (bw->bit_pos >= 8) {
            bw->bit_pos = 0;
            bw->byte_pos++;
            if (bw->byte_pos < bw->cap)
                bw->buf[bw->byte_pos] = 0;
        }
    }
    return MZ_OK;
}

static size_t
bw_bytes_used(const struct bit_writer *bw)
{
    return bw->byte_pos + (bw->bit_pos > 0 ? 1 : 0);
}

/* ---- Bitstream reader with lookahead buffer ---- */

struct bit_reader {
    const uint8_t *buf;
    size_t len_bytes;
    size_t byte_pos;
    int bit_pos;
    uint32_t hold;
    int hold_bits;
};

static void
br_init(struct bit_reader *br, const uint8_t *buf, size_t len)
{
    br->buf = buf;
    br->len_bytes = len;
    br->byte_pos = 0;
    br->bit_pos = 0;
    br->hold = 0;
    br->hold_bits = 0;
}

static int
br_fill(struct bit_reader *br, int n)
{
    while (br->hold_bits < n) {
        if (br->byte_pos >= br->len_bytes) return MZ_ERR_STREAM;
        br->hold = (br->hold << 8) | br->buf[br->byte_pos];
        br->hold_bits += 8;
        br->byte_pos++;
    }
    return MZ_OK;
}

static inline unsigned
br_peek(const struct bit_reader *br, int n)
{
    return (unsigned)(br->hold >> (br->hold_bits - n));
}

static inline void
br_consume(struct bit_reader *br, int n)
{
    br->hold_bits -= n;
    br->hold &= ((1u << br->hold_bits) - 1);
}

/* ---- Decode lookup table ---- */

#define DECODE_TABLE_BITS 10
#define DECODE_TABLE_SIZE (1 << DECODE_TABLE_BITS)

struct decode_entry {
    int sym;
    int len;
};

static void
build_decode_table(struct decode_entry table[DECODE_TABLE_SIZE],
                   const unsigned *codes, const int *lens)
{
    memset(table, 0, DECODE_TABLE_SIZE * sizeof(struct decode_entry));

    struct sym_len sorted[MAX_SYMBOLS];
    int n = 0;
    for (int i = 0; i < MAX_SYMBOLS; i++) {
        if (lens[i] > 0 && lens[i] <= DECODE_TABLE_BITS) {
            sorted[n].sym = i;
            sorted[n].len = lens[i];
            n++;
        }
    }
    qsort(sorted, (size_t)n, sizeof(struct sym_len), sl_cmp);

    for (int i = 0; i < n; i++) {
        int sym = sorted[i].sym;
        int len = lens[sym];
        unsigned code = codes[sym];
        int shift = DECODE_TABLE_BITS - len;
        for (unsigned j = 0; j < (1u << shift); j++) {
            unsigned idx = (code << shift) | j;
            if (idx < DECODE_TABLE_SIZE) {
                table[idx].sym = sym;
                table[idx].len = len;
            }
        }
    }
}

/* Decode one symbol using lookup table + tree fallback.
 * Returns symbol (0-255) or -1 on error. */
static int
decode_sym(struct bit_reader *br,
           const struct decode_entry table[DECODE_TABLE_SIZE],
           const struct huf_node *nodes, int root_idx,
           const int *lens)
{
    (void)lens;
    /* Try table lookup first */
    if (br_fill(br, DECODE_TABLE_BITS) == MZ_OK) {
        unsigned key = br_peek(br, DECODE_TABLE_BITS);
        if (key < DECODE_TABLE_SIZE && table[key].len > 0) {
            br_consume(br, table[key].len);
            return table[key].sym;
        }
    }

    /* Fallback: read bit by bit through the tree */
    int idx = root_idx;
    while (idx >= 0 && nodes[idx].sym < 0) {
        if (br_fill(br, 1) != MZ_OK) return -1;
        int bit = (int)br_peek(br, 1);
        br_consume(br, 1);
        idx = (bit == 0) ? nodes[idx].left : nodes[idx].right;
    }
    if (idx < 0 || nodes[idx].sym < 0) return -1;
    return nodes[idx].sym;
}

/* ---- public API ---- */

int
mz_huf_compress(const unsigned char *in, size_t inlen,
                unsigned char *out, size_t *outlen)
{
    if (!in || !out || !outlen) return MZ_ERR_PARAM;
    if (inlen == 0) return MZ_ERR_PARAM;

    unsigned freqs[MAX_SYMBOLS] = {0};
    for (size_t i = 0; i < inlen; i++)
        freqs[in[i]]++;

    struct huf_node nodes[512];
    int node_count = 0;
    if (!build_tree(nodes, &node_count, freqs))
        return MZ_ERR_DATA;

    int lengths[MAX_SYMBOLS] = {0};
    if (node_count == 1 && nodes[0].sym >= 0) {
        lengths[nodes[0].sym] = 1;
    } else {
        calc_lengths(nodes, node_count - 1, 0, lengths);
    }

    unsigned codes[MAX_SYMBOLS];
    int code_lens[MAX_SYMBOLS];
    memset(codes, 0, sizeof(codes));
    memset(code_lens, 0, sizeof(code_lens));
    make_canonical(codes, code_lens, lengths, MAX_CODE_LEN);

    size_t op = 0;
    size_t max_out = *outlen;
    if (op + 2 + MAX_SYMBOLS > max_out) return MZ_ERR_STREAM;
    out[op++] = MAX_SYMBOLS & 0xFF;
    out[op++] = (MAX_SYMBOLS >> 8) & 0xFF;
    for (int i = 0; i < MAX_SYMBOLS; i++)
        out[op++] = (uint8_t)code_lens[i];

    struct bit_writer bw;
    bw_init(&bw, out + op, max_out - op);

    for (size_t i = 0; i < inlen; i++) {
        int sym = in[i];
        int ret = bw_write_bits(&bw, codes[sym], code_lens[sym]);
        if (ret != MZ_OK) return ret;
    }

    *outlen = op + bw_bytes_used(&bw);

    /* If compressed (with table overhead) >= original, return raw data */
    if (*outlen >= inlen) {
        memmove(out, in, inlen);
        *outlen = inlen;
        return MZ_ERR_DATA;  /* -2: signals no compression */
    }
    return MZ_OK;
}

int
mz_huf_decompress(const unsigned char *in, size_t inlen,
                  unsigned char *out, size_t *outlen)
{
    if (!in || !out || !outlen) return MZ_ERR_PARAM;
    if (inlen < 2 + MAX_SYMBOLS) return MZ_ERR_DATA;

    size_t ip = 0;
    int num_syms = (int)in[ip] | ((int)in[ip + 1] << 8);
    ip += 2;
    if (num_syms != MAX_SYMBOLS) return MZ_ERR_DATA;

    int code_lens[MAX_SYMBOLS] = {0};
    for (int i = 0; i < num_syms; i++) {
        int cl = (int)in[ip++];
        code_lens[i] = (cl >= 0 && cl <= MAX_CODE_LEN) ? cl : 0;
    }

    struct sym_len sorted[MAX_SYMBOLS];
    int n_syms = 0;
    for (int i = 0; i < MAX_SYMBOLS; i++) {
        if (code_lens[i] > 0) {
            sorted[n_syms].sym = i;
            sorted[n_syms].len = code_lens[i];
            n_syms++;
        }
    }

    unsigned codes[MAX_SYMBOLS];
    int clens[MAX_SYMBOLS];
    memset(codes, 0, sizeof(codes));
    memset(clens, 0, sizeof(clens));
    if (n_syms > 0) {
        qsort(sorted, (size_t)n_syms, sizeof(struct sym_len), sl_cmp);
        unsigned code = 0;
        int prev_len = 0;
        for (int i = 0; i < n_syms; i++) {
            int sym = sorted[i].sym;
            int len = sorted[i].len;
            code <<= (len - prev_len);
            codes[sym] = code;
            clens[sym] = len;
            code++;
            prev_len = len;
        }
    }

    /* Rebuild Huffman tree from canonical codes */
    struct huf_node nodes[512];
    memset(nodes, 0, sizeof(nodes));
    int root = -1;

    if (n_syms > 0) {
        int node_count = 0;
        /* Bound the number of nodes we may allocate: code lengths come
         * from the input stream, so a hostile table (e.g. 256 symbols of
         * length 32) would otherwise overflow the stack-allocated
         * nodes[512] array. */
        int max_nodes = (int)(sizeof(nodes) / sizeof(nodes[0]));
        root = node_count++;
        nodes[root].sym = -1;
        nodes[root].left = -1;
        nodes[root].right = -1;

        for (int si = 0; si < n_syms; si++) {
            int sym = sorted[si].sym;
            unsigned canon_code = codes[sym];
            int len = clens[sym];

            int idx = root;
            for (int b = len - 1; b >= 0; b--) {
                int bit = (int)((canon_code >> b) & 1);
                if (bit == 0) {
                    if (nodes[idx].left < 0) {
                        if (node_count >= max_nodes) {
                            return MZ_ERR_DATA;
                        }
                        int nid = node_count++;
                        nodes[nid].sym = -1;
                        nodes[nid].left = -1;
                        nodes[nid].right = -1;
                        nodes[idx].left = nid;
                    }
                    idx = nodes[idx].left;
                } else {
                    if (nodes[idx].right < 0) {
                        if (node_count >= max_nodes) {
                            return MZ_ERR_DATA;
                        }
                        int nid = node_count++;
                        nodes[nid].sym = -1;
                        nodes[nid].left = -1;
                        nodes[nid].right = -1;
                        nodes[idx].right = nid;
                    }
                    idx = nodes[idx].right;
                }
            }
            nodes[idx].sym = sym;
        }
    }

    struct decode_entry dtable[DECODE_TABLE_SIZE];
    build_decode_table(dtable, codes, clens);

    struct bit_reader br;
    br_init(&br, in + ip, inlen - ip);
    size_t op = 0;

    while (op < *outlen) {
        int sym = decode_sym(&br, dtable, nodes, root, clens);
        if (sym < 0) break;
        out[op++] = (unsigned char)sym;
    }

    *outlen = op;
    return MZ_OK;
}
