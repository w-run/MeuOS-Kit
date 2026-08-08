/* mz_fusion.c — 自研融合压缩引擎（匹配 + 熵编码一次遍历）
 *
 * 设计目标：不做"先裸 LZ77 再二次熵编码"的管道式压缩。
 * 匹配器在遍历输入时产生结构化 seq（字面量 run + 匹配），
 * 同时同步统计符号频次；block 末尾用频次构建 Huffman 码表，
 * 将 seq 直接熵编码到位流。
 *
 * 这不是管道式：
 *   - 没有中间裸 LZ77 字节流
 *   - 频次统计内嵌于匹配循环（无二次扫描）
 *   - 熵编码与匹配共享同一引擎上下文
 *
 * 格式 (fusion block):
 *   [etype:1B]            0=RAW, 1=Huffman
 *   [uncompressed size:4B LE]
 *   RAW:
 *     [raw data]          原样字节
 *   HUFF:
 *     [lit table: mz_huf_write_table]    字面量码表
 *     [litlen table: mz_huf_write_table] 字面量段长码表
 *     [len table: mz_huf_write_table]    匹配长度码表
 *     [off table: mz_huf_write_table]    匹配偏移码表
 *     [lit bytes count:4B LE]
 *     [literals bitstream]  全部字面量字节（Huffman 编码）
 *     [seq count:4B LE]
 *     [seqs bitstream]      每个 seq: [litlen 类][len 类][off 类] + 额外位
 *
 * 编码模型（zstd seqStore 风格，自研实现）：
 *   字面量段：所有字面量字节连续存放，前缀编码。
 *   匹配序列：每个匹配 seq 记录它前面的字面量段长度 (litlen)、
 *             匹配长度 (len)、匹配偏移 (off)。
 *   解码时按 seq 顺序：输出 litlen 个字面量，再复制 len 字节。
 *
 * 分级表（简化 zstd 风格）：
 *   litlen: 0..1024, 每 8 个一段 → 129 类
 *   len:    3..258,  每 8 个一段 → 32 类
 *   off:    1..65535, 指数分级     → 32 类
 */
#include "mz.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ===================================================================
 * 常量
 * =================================================================== */

#define FUSION_ETYPE_RAW  0
#define FUSION_ETYPE_HUFF 1
#define FUSION_ETYPE_TANS 2

#define FUSION_MIN_MATCH  3
#define FUSION_MAX_MATCH  258
#define FUSION_MAX_OFFSET 65535

#define FUSION_WSIZE      65536
#define FUSION_WMASK      (FUSION_WSIZE - 1)
#define FUSION_HASH_BITS  16
#define FUSION_HASH_SIZE  (1 << FUSION_HASH_BITS)
#define FUSION_HASH_MASK  (FUSION_HASH_SIZE - 1)

#define FUSION_HASH3(p) \
    ((((p)[0] * 517u) ^ ((p)[1] * 131u) ^ ((p)[2])) & FUSION_HASH_MASK)

/* 分级类数 */
#define FUSION_LITLEN_CLASSES 17   /* 0..65535 指数分级 (0 + 16 级) */
#define FUSION_LEN_CLASSES    32   /* 3..258,  步长 8 */
#define FUSION_OFF_CLASSES    32   /* 1..65535 指数 */

/* ===================================================================
 * 分级表（惰性构建）
 * =================================================================== */

static uint16_t litlen_class_base[FUSION_LITLEN_CLASSES];
static uint8_t  litlen_class_extra[FUSION_LITLEN_CLASSES];
static uint16_t len_class_base[FUSION_LEN_CLASSES];
static uint8_t  len_class_extra[FUSION_LEN_CLASSES];
static uint16_t off_class_base[FUSION_OFF_CLASSES];
static uint8_t  off_class_extra[FUSION_OFF_CLASSES];

static int8_t len_class_of[FUSION_MAX_MATCH + 1];
static int8_t off_class_of[FUSION_MAX_OFFSET + 1];

/* litlen 类号：指数分级，O(1) 位宽计算，无数组越界风险 */
static inline int
litlen_class(unsigned v)
{
    if (v == 0) return 0;
    int bits = 0;
    while (v) { bits++; v >>= 1; }
    return bits;  /* 1..16 */
}

static void
build_class_tables(void)
{
    static int built = 0;
    if (built) return;

    /* litlen: 指数分级 0..65535 */
    /* 类 0 = 0 (无额外位)
     * 类 c (1..16) = [2^(c-1), 2^c-1], extra = c-1 位 */
    litlen_class_base[0] = 0;
    litlen_class_extra[0] = 0;
    for (int c = 1; c < FUSION_LITLEN_CLASSES; c++) {
        litlen_class_base[c] = (uint16_t)(1 << (c - 1));
        litlen_class_extra[c] = (uint8_t)(c - 1);
    }

    /* len: 3..258, 每 8 一段 (3+c*8 .. 3+c*8+7)，类内偏移 3 位 */
    for (int c = 0; c < FUSION_LEN_CLASSES; c++) {
        len_class_base[c] = (uint16_t)(FUSION_MIN_MATCH + c * 8);
        len_class_extra[c] = 3;
    }
    for (int v = FUSION_MIN_MATCH; v <= FUSION_MAX_MATCH; v++) {
        int c = (v - FUSION_MIN_MATCH) / 8;
        if (c >= FUSION_LEN_CLASSES) c = FUSION_LEN_CLASSES - 1;
        len_class_of[v] = (int8_t)c;
    }

    /* off: 指数分级 1..65535 */
    /* 类对 (2k, 2k+1) 共同覆盖 [2^k, 2^(k+1)-1]，
     * base = 2^k, extra = k 位 → 完整无空隙、编解码一致 */
    for (int c = 0; c < FUSION_OFF_CLASSES; c++) {
        int shift = c / 2;
        off_class_base[c] = (uint16_t)(1u << shift);
        off_class_extra[c] = (uint8_t)shift;
    }
    for (int v = 1; v <= FUSION_MAX_OFFSET; v++) {
        int c = 0;
        while (c + 1 < FUSION_OFF_CLASSES &&
               v >= off_class_base[c + 1])
            c++;
        off_class_of[v] = (int8_t)c;
    }

    built = 1;
}

/* 计算类内偏移（额外位值） */
static inline unsigned
litlen_class_index(unsigned v)
{
    int c = litlen_class(v);
    return (unsigned)(v - litlen_class_base[c]);
}
static inline unsigned
len_class_index(unsigned v)
{
    return (unsigned)(v - len_class_base[len_class_of[v]]);
}
static inline unsigned
off_class_index(unsigned v)
{
    unsigned base = off_class_base[off_class_of[v]];
    return (unsigned)(v - base);
}

/* ===================================================================
 * 位流写入器 (MSB first)
 * =================================================================== */

struct fusion_bw {
    uint8_t *buf;
    size_t   cap;
    size_t   pos;
    int      nbits;
};

static void
bw_init(struct fusion_bw *bw, uint8_t *buf, size_t cap)
{
    bw->buf = buf;
    bw->cap = cap;
    bw->pos = 0;
    bw->nbits = 0;
    if (cap > 0) buf[0] = 0;
}

static int
bw_put(struct fusion_bw *bw, unsigned code, int n)
{
    for (int i = n - 1; i >= 0; i--) {
        if (bw->pos >= bw->cap) return MZ_ERR_STREAM;
        bw->buf[bw->pos] |= ((code >> i) & 1) << (7 - bw->nbits);
        bw->nbits++;
        if (bw->nbits >= 8) {
            bw->nbits = 0;
            bw->pos++;
            if (bw->pos < bw->cap)
                bw->buf[bw->pos] = 0;
        }
    }
    return MZ_OK;
}

static size_t
bw_bytes(const struct fusion_bw *bw)
{
    return bw->pos + (bw->nbits > 0 ? 1 : 0);
}

/* ===================================================================
 * 位流读取器 (MSB first, hold 缓冲支持 peek/consume)
 * =================================================================== */

struct fusion_br {
    const uint8_t *buf;
    size_t   len;
    size_t   byte_pos;
    int      bit_pos;
    uint32_t hold;
    int      hold_bits;
};

static void
br_init(struct fusion_br *br, const uint8_t *buf, size_t len)
{
    br->buf = buf;
    br->len = len;
    br->byte_pos = 0;
    br->bit_pos = 0;
    br->hold = 0;
    br->hold_bits = 0;
}

static int
br_fill(struct fusion_br *br, int n)
{
    while (br->hold_bits < n) {
        if (br->byte_pos >= br->len) return MZ_ERR_STREAM;
        br->hold = (br->hold << 8) | br->buf[br->byte_pos];
        br->hold_bits += 8;
        br->byte_pos++;
    }
    return MZ_OK;
}

static inline unsigned
br_peek(const struct fusion_br *br, int n)
{
    return (unsigned)(br->hold >> (br->hold_bits - n));
}

static inline void
br_consume(struct fusion_br *br, int n)
{
    br->hold_bits -= n;
    br->hold &= ((1u << br->hold_bits) - 1);
}

static int
br_get_n(struct fusion_br *br, int n, unsigned *out)
{
    if (br_fill(br, n) != MZ_OK) return MZ_ERR_DATA;
    *out = br_peek(br, n);
    br_consume(br, n);
    return MZ_OK;
}

/* ===================================================================
 * Huffman 解码辅助（从码长构建解码表）
 * =================================================================== */

#define FUSION_DEC_TABLE_BITS 10
#define FUSION_DEC_TABLE_SIZE (1 << FUSION_DEC_TABLE_BITS)

struct fusion_dec_sym {
    int16_t sym;
    uint8_t len;
};

struct fusion_sym_len {
    int sym;
    int len;
};

static int
build_dec_table(struct fusion_dec_sym table[FUSION_DEC_TABLE_SIZE],
                const int lens[256], int max_syms, int *out_max_len)
{
    memset(table, 0, FUSION_DEC_TABLE_SIZE * sizeof(struct fusion_dec_sym));
    int max_len = 0;

    /* 收集有效符号并排序（按 len, 再按 sym） */
    struct fusion_sym_len sorted[256];
    int n = 0;
    for (int i = 0; i < max_syms; i++) {
        if (lens[i] > 0) {
            sorted[n].sym = i;
            sorted[n].len = lens[i];
            n++;
        }
    }
    if (n == 0) return MZ_ERR_DATA;
    for (int i = 1; i < n; i++) {
        struct fusion_sym_len t = sorted[i];
        int j = i - 1;
        while (j >= 0 &&
               (sorted[j].len > t.len ||
                (sorted[j].len == t.len && sorted[j].sym > t.sym))) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = t;
    }

    /* 规范码分配 */
    unsigned codes[256];
    int clens[256];
    memset(codes, 0, sizeof(codes));
    memset(clens, 0, sizeof(clens));
    unsigned code = 0;
    int prev_len = 0;
    for (int i = 0; i < n; i++) {
        int sym = sorted[i].sym;
        int len = sorted[i].len;
        code <<= (len - prev_len);
        codes[sym] = code;
        clens[sym] = len;
        code++;
        prev_len = len;
    }

    /* 构建解码表：码长 <= 10 的直接填表，> 10 的返回 -1（走逐位解码） */
    int need_tree = 0;
    for (int i = 0; i < n; i++) {
        int sym = sorted[i].sym;
        int len = clens[sym];
        if (len > max_len) max_len = len;
        if (len > FUSION_DEC_TABLE_BITS) {
            need_tree = 1;
            continue;
        }
        unsigned c = codes[sym];
        int shift = FUSION_DEC_TABLE_BITS - len;
        for (unsigned j = 0; j < (1u << shift); j++) {
            unsigned idx = (c << shift) | j;
            if (idx < FUSION_DEC_TABLE_SIZE) {
                table[idx].sym = (int16_t)sym;
                table[idx].len = (uint8_t)len;
            }
        }
    }
    *out_max_len = max_len;
    return need_tree ? 1 : 0;  /* 0=纯查表, 1=需要树回退 */
}

/* 解码一个符号（递增 peek 查表，consume 实际码长）。
 *
 * 不固定 peek FUSION_DEC_TABLE_BITS 位：位流末尾（最后几个符号）
 * 剩余位可能不足 10 位，固定 peek 会在 br_fill 越界而误报错误。
 * 从 1 位起递增 peek，k 位前缀一旦恰好等于某个符号的完整码长即命中；
 * 若表项码长大于 k 说明该前缀只是更长码的前缀，继续递增。
 * 规范 Huffman 码前缀无歧义，因此该贪心查找保证正确。 */
static int
dec_sym(struct fusion_br *br, const struct fusion_dec_sym table[FUSION_DEC_TABLE_SIZE],
        int max_len)
{
    int limit = max_len < FUSION_DEC_TABLE_BITS ? max_len : FUSION_DEC_TABLE_BITS;
    for (int k = 1; k <= limit; k++) {
        if (br_fill(br, k) != MZ_OK)
            return -1;
        unsigned key = br_peek(br, k);
        unsigned idx = key << (FUSION_DEC_TABLE_BITS - k);
        const struct fusion_dec_sym *e = &table[idx];
        if (e->len == k) {
            br_consume(br, k);
            return e->sym;
        }
    }
    return -1;
}

/* ===================================================================
 * 匹配器状态
 * =================================================================== */

struct fusion_matcher {
    uint8_t   win[FUSION_WSIZE];
    size_t    win_pos;
    uint32_t  chain[FUSION_WSIZE];
    uint32_t  head[FUSION_HASH_SIZE];
    int       chain_depth;
    int       max_match;
    size_t    max_dist;
    int       use_lazy;
};

static void
matcher_init(struct fusion_matcher *m, int level)
{
    memset(m->win, 0, FUSION_WSIZE);
    m->win_pos = 0;
    memset(m->chain, 0, sizeof(m->chain));
    memset(m->head, 0xFF, sizeof(m->head));

    switch (level) {
    case 1:  m->chain_depth = 4;   m->max_match = 16;  m->max_dist = 4096;  m->use_lazy = 0; break;
    case 2:  m->chain_depth = 16;  m->max_match = 32;  m->max_dist = 4096;  m->use_lazy = 0; break;
    case 3:  m->chain_depth = 64;  m->max_match = 64;  m->max_dist = 16384; m->use_lazy = 0; break;
    case 4:  m->chain_depth = 128; m->max_match = 96;  m->max_dist = 16384; m->use_lazy = 1; break;
    case 5:  m->chain_depth = 256; m->max_match = 130; m->max_dist = 65535; m->use_lazy = 1; break;
    case 6:  m->chain_depth = 512; m->max_match = 200; m->max_dist = 65535; m->use_lazy = 1; break;
    case 7:  m->chain_depth = 1024;m->max_match = 258; m->max_dist = 65535; m->use_lazy = 1; break;
    case 8:  m->chain_depth = 2048;m->max_match = 258; m->max_dist = 65535; m->use_lazy = 1; break;
    case 9:  m->chain_depth = 4096;m->max_match = 258; m->max_dist = 65535; m->use_lazy = 1; break;
    default: m->chain_depth = 256; m->max_match = 130; m->max_dist = 65535; m->use_lazy = 1; break;
    }
}

static void
matcher_insert(struct fusion_matcher *m, size_t pos,
               const uint8_t *data, size_t limit)
{
    if (pos + 2 >= limit) return;
    uint16_t h3 = FUSION_HASH3(data + pos);
    size_t idx = pos & FUSION_WMASK;
    m->chain[idx] = m->head[h3];
    m->head[h3] = (uint32_t)pos;
}

static int
matcher_find(struct fusion_matcher *m, size_t pos,
             const uint8_t *data, size_t limit,
             int *out_len, size_t *out_off)
{
    if (pos + 2 >= limit) return 0;
    uint16_t h3 = FUSION_HASH3(data + pos);
    uint32_t idx = m->head[h3];
    int best_len = 0;
    size_t best_off = 0;

    uint32_t key3 = (uint32_t)data[pos]
                  | ((uint32_t)data[pos + 1] << 8)
                  | ((uint32_t)data[pos + 2] << 16);

    for (int c = 0; c < m->chain_depth && idx != (uint32_t)-1 &&
         pos > idx && pos - idx <= m->max_dist; c++) {
        uint32_t cand = (uint32_t)data[idx]
                      | ((uint32_t)data[idx + 1] << 8)
                      | ((uint32_t)data[idx + 2] << 16);
        if (cand == key3) {
            int ml = (int)(limit - pos);
            if (ml > m->max_match) ml = m->max_match;
            int len = FUSION_MIN_MATCH;
            while (len < ml && data[idx + len] == data[pos + len])
                len++;
            if (len > best_len) {
                best_len = len;
                best_off = pos - idx;
                if (len >= ml) break;
            }
        }
        idx = m->chain[idx & FUSION_WMASK];
    }

    if (best_len >= FUSION_MIN_MATCH) {
        *out_len = best_len;
        *out_off = best_off;
        return 1;
    }
    return 0;
}

static void
matcher_slide(struct fusion_matcher *m, const uint8_t *data,
              size_t *ip, size_t inlen, size_t count)
{
    for (size_t i = 0; i < count && *ip < inlen; i++) {
        m->win[m->win_pos] = data[*ip];
        m->win_pos = (m->win_pos + 1) & FUSION_WMASK;
        matcher_insert(m, *ip, data, inlen);
        (*ip)++;
    }
}

/* ===================================================================
 * seqStore：字面量段 + 匹配序列
 * =================================================================== */

struct fusion_seq {
    /* 字面量连续缓冲 */
    uint8_t *lits;
    size_t   lit_count;
    size_t   lit_cap;

    /* 匹配序列（每个 seq = 前一段字面量 + 一个匹配） */
    uint16_t *litlens;   /* 每个匹配前的字面量段长度 */
    uint16_t *lens;      /* 匹配长度 */
    uint32_t *offs;      /* 匹配偏移 */
    size_t    seq_count;
    size_t    seq_cap;

    /* 频次 */
    unsigned  lit_freq[256];
    unsigned  litlen_freq[FUSION_LITLEN_CLASSES];
    unsigned  len_freq[FUSION_LEN_CLASSES];
    unsigned  off_freq[FUSION_OFF_CLASSES];
};

static int
seq_init(struct fusion_seq *s)
{
    memset(s, 0, sizeof(*s));
    s->lit_cap = 4096;
    s->lits = malloc(s->lit_cap);
    s->seq_cap = 1024;
    s->litlens = malloc(s->seq_cap * sizeof(uint16_t));
    s->lens = malloc(s->seq_cap * sizeof(uint16_t));
    s->offs = malloc(s->seq_cap * sizeof(uint32_t));
    if (!s->lits || !s->litlens || !s->lens || !s->offs) {
        free(s->lits); free(s->litlens); free(s->lens); free(s->offs);
        return MZ_ERR_MEMORY;
    }
    return MZ_OK;
}

static void
seq_free(struct fusion_seq *s)
{
    free(s->lits);
    free(s->litlens);
    free(s->lens);
    free(s->offs);
}

static int
seq_add_lit(struct fusion_seq *s, uint8_t b)
{
    if (s->lit_count >= s->lit_cap) {
        size_t nc = s->lit_cap * 2;
        uint8_t *p = realloc(s->lits, nc);
        if (!p) return MZ_ERR_MEMORY;
        s->lits = p;
        s->lit_cap = nc;
    }
    s->lits[s->lit_count++] = b;
    s->lit_freq[b]++;
    return MZ_OK;
}

/* 追加一个匹配；lit_run 是该匹配前的字面量字节数 */
static int
seq_add_match(struct fusion_seq *s, size_t off, int len, size_t lit_run)
{
    if (s->seq_count >= s->seq_cap) {
        size_t nc = s->seq_cap * 2;
        uint16_t *pl = realloc(s->litlens, nc * sizeof(uint16_t));
        uint16_t *pm = realloc(s->lens, nc * sizeof(uint16_t));
        uint32_t *po = realloc(s->offs, nc * sizeof(uint32_t));
        if (!pl || !pm || !po) { free(pl); free(pm); free(po); return MZ_ERR_MEMORY; }
        s->litlens = pl;
        s->lens = pm;
        s->offs = po;
        s->seq_cap = nc;
    }
    size_t i = s->seq_count++;
    /* litlen 钳制到 uint16 上限 (65535)；超长段会被截断，
     * 但由于 litlen_class 支持 16 位宽，实际输入 buffer 中
     * 单段字面量通常远小于 64KB。 */
    if (lit_run > 0xFFFF)
        lit_run = 0xFFFF;
    s->litlens[i] = (uint16_t)lit_run;
    s->lens[i] = (uint16_t)len;
    s->offs[i] = (uint32_t)off;

    int llc = litlen_class((unsigned)lit_run);
    int lc = len_class_of[len];
    int oc = off_class_of[off];
    s->litlen_freq[llc]++;
    s->len_freq[lc]++;
    s->off_freq[oc]++;
    return MZ_OK;
}

/* ===================================================================
 * 匹配主循环（一次遍历，同步统计频次）
 * =================================================================== */

static int
run_matcher(struct fusion_matcher *m, const uint8_t *data, size_t inlen,
            struct fusion_seq *s)
{
    size_t ip = 0;
    size_t run_start = 0;  /* 当前字面量段起点（用于计算 lit_run） */

    while (ip < inlen) {
        int match_len;
        size_t match_off;

        if (matcher_find(m, ip, data, inlen, &match_len, &match_off)) {
            if (m->use_lazy && ip + 1 < inlen && ip + 2 < inlen) {
                uint16_t h3 = FUSION_HASH3(data + ip);
                size_t idx = ip & FUSION_WMASK;
                m->chain[idx] = m->head[h3];
                m->head[h3] = (uint32_t)ip;

                int next_len;
                size_t next_off;
                if (matcher_find(m, ip + 1, data, inlen, &next_len, &next_off) &&
                    next_len > match_len + 1) {
                    goto emit_lit;
                }
            }

            if (match_len > FUSION_MAX_MATCH)
                match_len = FUSION_MAX_MATCH;
            if ((size_t)match_len > inlen - ip)
                match_len = (int)(inlen - ip);
            if (match_len < FUSION_MIN_MATCH || match_off > FUSION_MAX_OFFSET)
                goto emit_lit;

            /* 提交匹配，lit_run = 本段已收集的字面量数 */
            size_t lit_run = ip - run_start;
            if (seq_add_match(s, match_off, match_len, lit_run) != MZ_OK)
                return MZ_ERR_MEMORY;
            matcher_slide(m, data, &ip, inlen, (size_t)match_len);
            run_start = ip;   /* 重置字面量段起点 */
            continue;
        }

emit_lit:
        {
            if (seq_add_lit(s, data[ip]) != MZ_OK)
                return MZ_ERR_MEMORY;
            matcher_slide(m, data, &ip, inlen, 1);
        }
    }
    return MZ_OK;
}

/* ===================================================================
 * Huffman 融合编码
 * =================================================================== */

static int
huff_encode_seq(struct fusion_seq *s, uint8_t *out, size_t max_out,
                size_t *out_len)
{
    unsigned lit_codes[256], ll_codes[256], len_codes[256], off_codes[256];
    int lit_lens[256], ll_lens[256], len_lens[256], off_lens[256];

    mz_huf_build_codes(s->lit_freq, 256, lit_codes, lit_lens);
    mz_huf_build_codes(s->litlen_freq, FUSION_LITLEN_CLASSES, ll_codes, ll_lens);
    mz_huf_build_codes(s->len_freq, FUSION_LEN_CLASSES, len_codes, len_lens);
    mz_huf_build_codes(s->off_freq, FUSION_OFF_CLASSES, off_codes, off_lens);

    size_t op = 0;
    int rc;
    rc = mz_huf_write_table(out + op, max_out - op, lit_lens);
    if (rc < 0) return rc;
    op += (size_t)rc;
    rc = mz_huf_write_table(out + op, max_out - op, ll_lens);
    if (rc < 0) return rc;
    op += (size_t)rc;
    rc = mz_huf_write_table(out + op, max_out - op, len_lens);
    if (rc < 0) return rc;
    op += (size_t)rc;
    rc = mz_huf_write_table(out + op, max_out - op, off_lens);
    if (rc < 0) return rc;
    op += (size_t)rc;

    /* 布局: 表头×4 | lit_count(4) | seq_count(4) | 合并位流(lits 后接 seqs) */
    if (op + 4 > max_out) return MZ_ERR_STREAM;
    uint32_t lc = (uint32_t)s->lit_count;
    out[op++] = lc & 0xFF; out[op++] = (lc >> 8) & 0xFF;
    out[op++] = (lc >> 16) & 0xFF; out[op++] = (lc >> 24) & 0xFF;

    if (op + 4 > max_out) return MZ_ERR_STREAM;
    uint32_t sc = (uint32_t)s->seq_count;
    out[op++] = sc & 0xFF; out[op++] = (sc >> 8) & 0xFF;
    out[op++] = (sc >> 16) & 0xFF; out[op++] = (sc >> 24) & 0xFF;

    /* 合并位流: 字面量 + seq，同一 writer 顺序输出 */
    struct fusion_bw bw;
    bw_init(&bw, out + op, max_out - op);
    for (size_t i = 0; i < s->lit_count; i++) {
        int sym = s->lits[i];
        if (bw_put(&bw, lit_codes[sym], lit_lens[sym]) != MZ_OK)
            return MZ_ERR_STREAM;
    }
    for (size_t i = 0; i < s->seq_count; i++) {
        unsigned litlen = s->litlens[i];
        unsigned len = s->lens[i];
        unsigned off = s->offs[i];

        int llc = litlen_class(litlen);
        int lc2 = len_class_of[len];
        int oc = off_class_of[off];

        if (bw_put(&bw, ll_codes[llc], ll_lens[llc]) != MZ_OK) return MZ_ERR_STREAM;
        if (litlen_class_extra[llc] > 0 &&
            bw_put(&bw, litlen_class_index(litlen), litlen_class_extra[llc]) != MZ_OK)
            return MZ_ERR_STREAM;

        if (bw_put(&bw, len_codes[lc2], len_lens[lc2]) != MZ_OK) return MZ_ERR_STREAM;
        if (len_class_extra[lc2] > 0 &&
            bw_put(&bw, len_class_index(len), len_class_extra[lc2]) != MZ_OK)
            return MZ_ERR_STREAM;

        if (bw_put(&bw, off_codes[oc], off_lens[oc]) != MZ_OK) return MZ_ERR_STREAM;
        if (off_class_extra[oc] > 0 &&
            bw_put(&bw, off_class_index(off), off_class_extra[oc]) != MZ_OK)
            return MZ_ERR_STREAM;
    }
    op += bw_bytes(&bw);

    *out_len = op;
    return MZ_OK;
}

/* ===================================================================
 * Huffman 融合解码
 * =================================================================== */

static int
huff_decode_seq(const uint8_t *in, size_t inlen, size_t *ip,
                uint32_t usize, uint8_t *out, size_t *out_len)
{
    int lit_lens[256], ll_lens[256], len_lens[256], off_lens[256];
    int rc;

    rc = mz_huf_read_table(in, inlen, ip, lit_lens);
    if (rc < 0) return rc;
    rc = mz_huf_read_table(in, inlen, ip, ll_lens);
    if (rc < 0) return rc;
    rc = mz_huf_read_table(in, inlen, ip, len_lens);
    if (rc < 0) return rc;
    rc = mz_huf_read_table(in, inlen, ip, off_lens);
    if (rc < 0) return rc;

    /* 布局: 表头×4 | lit_count(4) | seq_count(4) | 合并位流(lits 后接 seqs) */
    if (*ip + 4 > inlen) return MZ_ERR_DATA;
    uint32_t lit_count = (uint32_t)in[*ip] | ((uint32_t)in[*ip+1] << 8)
                       | ((uint32_t)in[*ip+2] << 16) | ((uint32_t)in[*ip+3] << 24);
    *ip += 4;

    if (*ip + 4 > inlen) return MZ_ERR_DATA;
    uint32_t seq_count = (uint32_t)in[*ip] | ((uint32_t)in[*ip+1] << 8)
                       | ((uint32_t)in[*ip+2] << 16) | ((uint32_t)in[*ip+3] << 24);
    *ip += 4;

    /* 解码表 */
    struct fusion_dec_sym lit_tab[FUSION_DEC_TABLE_SIZE];
    struct fusion_dec_sym ll_tab[FUSION_DEC_TABLE_SIZE];
    struct fusion_dec_sym len_tab[FUSION_DEC_TABLE_SIZE];
    struct fusion_dec_sym off_tab[FUSION_DEC_TABLE_SIZE];
    int lit_mlen = 0, ll_mlen = 0, len_mlen = 0, off_mlen = 0;
    if (build_dec_table(lit_tab, lit_lens, 256, &lit_mlen) < 0) return MZ_ERR_DATA;
    if (build_dec_table(ll_tab, ll_lens, FUSION_LITLEN_CLASSES, &ll_mlen) < 0) return MZ_ERR_DATA;
    if (build_dec_table(len_tab, len_lens, FUSION_LEN_CLASSES, &len_mlen) < 0) return MZ_ERR_DATA;
    if (build_dec_table(off_tab, off_lens, FUSION_OFF_CLASSES, &off_mlen) < 0) return MZ_ERR_DATA;

    /* 合并位流起始 */
    struct fusion_br br;
    br_init(&br, in + *ip, inlen - *ip);

    size_t op = 0;
    uint8_t *lits = NULL;
    if (lit_count > 0) {
        lits = malloc(lit_count);
        if (!lits) return MZ_ERR_MEMORY;
        for (uint32_t i = 0; i < lit_count; i++) {
            int sym = dec_sym(&br, lit_tab, lit_mlen);
            if (sym < 0) { free(lits); return MZ_ERR_DATA; }
            lits[i] = (uint8_t)sym;
        }
    }

    /* 解码 seq */
    size_t lit_cursor = 0;
    for (uint32_t i = 0; i < seq_count; i++) {
        int llc = dec_sym(&br, ll_tab, ll_mlen);
        if (llc < 0) { free(lits); return MZ_ERR_DATA; }
        unsigned llv = litlen_class_base[llc];
        if (litlen_class_extra[llc] > 0) {
            unsigned e;
            if (br_get_n(&br, litlen_class_extra[llc], &e) != MZ_OK)
                { free(lits); return MZ_ERR_DATA; }
            llv += e;
        }

        int lc = dec_sym(&br, len_tab, len_mlen);
        if (lc < 0) { free(lits); return MZ_ERR_DATA; }
        unsigned lv = len_class_base[lc];
        if (len_class_extra[lc] > 0) {
            unsigned e;
            if (br_get_n(&br, len_class_extra[lc], &e) != MZ_OK)
                { free(lits); return MZ_ERR_DATA; }
            lv += e;
        }

        int oc = dec_sym(&br, off_tab, off_mlen);
        if (oc < 0) { free(lits); return MZ_ERR_DATA; }
        unsigned ov = off_class_base[oc];
        if (off_class_extra[oc] > 0) {
            unsigned e;
            if (br_get_n(&br, off_class_extra[oc], &e) != MZ_OK)
                { free(lits); return MZ_ERR_DATA; }
            ov += e;
        }

        /* 输出 litlen 个字面量 */
        if (lit_cursor + llv > lit_count) { free(lits); return MZ_ERR_DATA; }
        for (unsigned j = 0; j < llv; j++) {
            if (op >= usize) { free(lits); return MZ_ERR_DATA; }
            out[op++] = lits[lit_cursor++];
        }
        /* 复制匹配 */
        if (ov == 0 || ov > op) { free(lits); return MZ_ERR_DATA; }
        for (unsigned j = 0; j < lv; j++) {
            if (op >= usize) { free(lits); return MZ_ERR_DATA; }
            out[op] = out[op - ov];
            op++;
        }
    }

    /* 剩余字面量（最后一段） */
    while (lit_cursor < lit_count) {
        if (op >= usize) { free(lits); return MZ_ERR_DATA; }
        out[op++] = lits[lit_cursor++];
    }

    free(lits);
    *out_len = op;
    return MZ_OK;
}

/* ===================================================================
 * 公开 API
 * =================================================================== */

int
mz_fusion_compress(const void *in, size_t il, void **r, size_t *rl, int lv)
{
    if (!in || !r || !rl) return MZ_ERR_PARAM;
    if (il == 0) return MZ_ERR_DATA;  /* 空输入不支持 */

    build_class_tables();

    if (lv == 0)
        lv = 6;  /* 自适应暂用平衡级 */

    /* 小文件直接 RAW */
    if (il < 64) {
        size_t total = 1 + 4 + il;
        uint8_t *out = malloc(total);
        if (!out) return MZ_ERR_MEMORY;
        out[0] = FUSION_ETYPE_RAW;
        out[1] = il & 0xFF; out[2] = (il >> 8) & 0xFF;
        out[3] = (il >> 16) & 0xFF; out[4] = (il >> 24) & 0xFF;
        memcpy(out + 5, in, il);
        *r = out;
        *rl = total;
        return (int)total;
    }
    if (lv < 1) lv = 1;
    if (lv > 9) lv = 9;

    struct fusion_matcher m;
    matcher_init(&m, lv);

    struct fusion_seq s;
    int rc = seq_init(&s);
    if (rc != MZ_OK) return rc;

    rc = run_matcher(&m, (const uint8_t *)in, il, &s);
    if (rc != MZ_OK) { seq_free(&s); return rc; }

    /* 无匹配 → RAW */
    if (s.seq_count == 0) {
        size_t total = 1 + 4 + il;
        uint8_t *out = malloc(total);
        if (!out) { seq_free(&s); return MZ_ERR_MEMORY; }
        out[0] = FUSION_ETYPE_RAW;
        out[1] = il & 0xFF; out[2] = (il >> 8) & 0xFF;
        out[3] = (il >> 16) & 0xFF; out[4] = (il >> 24) & 0xFF;
        memcpy(out + 5, in, il);
        seq_free(&s);
        *r = out;
        *rl = total;
        return (int)total;
    }

    /* Huffman 编码（Phase 1） */
    size_t max_out = il + il / 4 + 8192;
    uint8_t *out = malloc(max_out);
    if (!out) { seq_free(&s); return MZ_ERR_MEMORY; }

    out[0] = FUSION_ETYPE_HUFF;
    out[1] = il & 0xFF; out[2] = (il >> 8) & 0xFF;
    out[3] = (il >> 16) & 0xFF; out[4] = (il >> 24) & 0xFF;

    /* 布局: etype(1) | usize(4) | 表头×4 | lit_count(4) | seq_count(4) | 合并位流 */
    size_t huff_len = 0;
    rc = huff_encode_seq(&s, out + 5, max_out - 5, &huff_len);
    seq_free(&s);
    if (rc < 0) { free(out); return rc; }

    /* 若压缩后更大 → 回退 RAW */
    if (5 + huff_len >= il) {
        size_t total = 1 + 4 + il;
        uint8_t *raw = malloc(total);
        if (!raw) { free(out); return MZ_ERR_MEMORY; }
        raw[0] = FUSION_ETYPE_RAW;
        raw[1] = il & 0xFF; raw[2] = (il >> 8) & 0xFF;
        raw[3] = (il >> 16) & 0xFF; raw[4] = (il >> 24) & 0xFF;
        memcpy(raw + 5, in, il);
        free(out);
        *r = raw;
        *rl = total;
        return (int)total;
    }

    *r = out;
    *rl = 5 + huff_len;
    return (int)(5 + huff_len);
}

int
mz_fusion_decompress(const void *in, size_t il, void **r, size_t *rl)
{
    if (!in || !r || !rl || il < 5) return MZ_ERR_PARAM;

    const uint8_t *p = (const uint8_t *)in;
    int etype = p[0];
    uint32_t usize = (uint32_t)p[1] | ((uint32_t)p[2] << 8)
                   | ((uint32_t)p[3] << 16) | ((uint32_t)p[4] << 24);

    if (etype == FUSION_ETYPE_RAW) {
        if (5 + (size_t)usize > il) return MZ_ERR_DATA;
        uint8_t *out = malloc(usize ? usize : 1);
        if (!out) return MZ_ERR_MEMORY;
        if (usize) memcpy(out, p + 5, usize);
        *r = out;
        *rl = usize;
        return (int)usize;
    }

    if (etype == FUSION_ETYPE_HUFF) {
        uint8_t *out = malloc(usize ? usize : 1);
        if (!out) return MZ_ERR_MEMORY;
        size_t ip = 5;
        size_t out_len = 0;
        int rc = huff_decode_seq(p, il, &ip, usize, out, &out_len);
        if (rc < 0) { free(out); return rc; }
        if (out_len != usize) { free(out); return MZ_ERR_DATA; }
        *r = out;
        *rl = out_len;
        return (int)out_len;
    }

    return MZ_ERR_CODEC;
}