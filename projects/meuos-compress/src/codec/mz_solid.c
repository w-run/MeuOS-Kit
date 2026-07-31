/* mz_solid.c — 固实压缩（Solid Compression）
 *
 * 设计：多个文件共享一个 LZ77 滑动窗口字典。
 * 第一个文件通过 SOLID_START block（type 2）写入，后续文件通过 SOLID_NEXT block（type 3）写入。
 * 每个文件的压缩数据仍然包含独立的 "mZ" 流头+结束标记，便于独立解压，
 * 但压缩时的查找范围跨越文件边界，提升压缩率。
 */
#include "mz.h"
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * LZ77 窗口参数 — 与 mz_lz77.c 保持一致
 * ================================================================ */
#define SOLID_WBITS    16
#define SOLID_WSIZE    (1u << SOLID_WBITS)      /* 65536 */
#define SOLID_WMASK    (SOLID_WSIZE - 1u)

#define SOLID_HBITS    16
#define SOLID_HSIZE    (1u << SOLID_HBITS)       /* 65536 */
#define SOLID_HMASK    (SOLID_HSIZE - 1u)

#define SOLID_MIN_MATCH    3
#define SOLID_MAX_MATCH    130
#define SOLID_MAX_OFFSET   ((1u << 16) - 1u)     /* 65535 */

/* ================================================================
 * struct mz_solid_ctx
 * 包含持久化的 LZ77 状态：滑动窗口 + 哈希链
 * ================================================================ */
struct mz_solid_ctx {
    uint8_t  win[SOLID_WSIZE];       /* 滑动窗口数据 */
    size_t   win_pos;                 /* 当前窗口写入位置 */
    uint16_t chain[SOLID_WSIZE];      /* 每位置的哈希链 */
    uint16_t head[SOLID_HSIZE];       /* 3 字节哈希 -> 最近位置 */
    int      level;                   /* 压缩级别 (1-9) */
    int      active;                  /* 1 = 流处于活跃状态 */
};

/* 3 字节哈希：将 24 位折叠为 16 位 */
#define SOLID_HASH3(p) \
    ((((p)[0] * 517u) ^ ((p)[1] * 131u) ^ ((p)[2])) & SOLID_HMASK)

/* ================================================================
 * 内部工具函数
 * ================================================================ */

/* level (1-9) → 哈希链遍历深度 */
static int chain_depth(int level)
{
    if (level <= 1) return 64;
    if (level >= 9) return 4096;
    return level * 128;  /* 256, 384, ..., 1024 */
}

/* 将位置 pos 通过 3 字节哈希插入哈希链 */
static void insert(struct mz_solid_ctx *ctx, size_t pos,
                   const uint8_t *data, size_t limit)
{
    if (pos + 2 >= limit) return;         /* 需要 ≥ 3 字节 */
    uint16_t h3 = SOLID_HASH3(data + pos);
    uint16_t idx = (uint16_t)(pos & SOLID_WMASK);
    ctx->chain[idx] = ctx->head[h3];
    ctx->head[h3] = (uint16_t)pos;
}

/* 在位置 pos 处寻找最佳匹配 */
static int find_match(struct mz_solid_ctx *ctx, size_t pos,
                      const uint8_t *data, size_t limit,
                      int *out_len, size_t *out_off)
{
    if (pos + 2 >= limit) return 0;

    uint16_t h3 = SOLID_HASH3(data + pos);
    uint16_t idx = ctx->head[h3];
    int best_len = 0;
    size_t best_off = 0;
    int max_chain = chain_depth(ctx->level);
    int max_len = SOLID_MAX_MATCH;
    /* 2 字节预过滤器 */
    uint16_t key2 = (uint16_t)data[pos] | ((uint16_t)data[pos + 1] << 8);

    for (int c = 0; c < max_chain && idx != 0xFFFF
         && pos > idx && (pos - idx) <= SOLID_MAX_OFFSET; c++) {
        uint16_t ck = (uint16_t)data[idx] | ((uint16_t)data[idx + 1] << 8);
        if (ck == key2) {
            int ml = (int)(limit - pos);
            if (ml > max_len) ml = max_len;
            int len = SOLID_MIN_MATCH;
            while (len < ml && data[idx + len] == data[pos + len])
                len++;
            if (len > best_len) {
                best_len = len;
                best_off = pos - idx;
                if (len >= ml) break;
            }
        }
        idx = ctx->chain[idx & SOLID_WMASK];
    }

    if (best_len >= SOLID_MIN_MATCH) {
        *out_len = best_len;
        *out_off = best_off;
        return 1;
    }
    return 0;
}

/* 将 count 字节滑入窗口并插入哈希链 */
static void slide_and_insert(struct mz_solid_ctx *ctx,
                             const uint8_t *data,
                             size_t *ip, size_t inlen,
                             size_t count)
{
    for (size_t i = 0; i < count && *ip < inlen; i++) {
        ctx->win[ctx->win_pos] = data[*ip];
        ctx->win_pos = (ctx->win_pos + 1) & SOLID_WMASK;
        insert(ctx, *ip, data, inlen);
        (*ip)++;
    }
}

/* ================================================================
 * 公开 API
 * ================================================================ */

int mz_solid_start(struct mz_solid_ctx **pctx, int level)
{
    if (!pctx)
        return MZ_ERR_PARAM;

    struct mz_solid_ctx *ctx = (struct mz_solid_ctx *)calloc(1, sizeof(*ctx));
    if (!ctx)
        return MZ_ERR_MEMORY;

    memset(ctx->win, 0, SOLID_WSIZE);
    ctx->win_pos = 0;
    memset(ctx->chain, 0, sizeof(ctx->chain));
    memset(ctx->head, 0xFF, sizeof(ctx->head));
    ctx->level = (level < 1) ? 1 : (level > 9 ? 9 : level);
    ctx->active = 1;

    *pctx = ctx;
    return MZ_OK;
}

int mz_solid_add(struct mz_solid_ctx *ctx, const void *data, size_t len,
                 void *output, size_t *out_len)
{
    if (!ctx || !ctx->active || !output || !out_len)
        return MZ_ERR_PARAM;
    if (!data && len > 0)
        return MZ_ERR_PARAM;

    const uint8_t *in = (const uint8_t *)data;
    uint8_t *out = (uint8_t *)output;
    size_t op = 0;

    /* 每个文件独立压缩：重置哈希链，使匹配只引用当前文件内已编码字节，
     * 与解压侧 mz_decompress_lz77 的独立窗口语义一致。
     * （此前 head/chain 跨文件保留，压缩侧可能引用前一文件字节——偏移
     *  超出解压方独立窗口 → 数据损坏/越界读。） */
    memset(ctx->chain, 0, sizeof(ctx->chain));
    memset(ctx->head, 0xFF, sizeof(ctx->head));
    ctx->win_pos = 0;

    /* 写入 "mZ" 流头：magic(2B) + 未压缩大小(4B LE) */
    out[op++] = 'm';
    out[op++] = 'Z';
    out[op++] = (uint8_t)(len & 0xFF);
    out[op++] = (uint8_t)((len >> 8) & 0xFF);
    out[op++] = (uint8_t)((len >> 16) & 0xFF);
    out[op++] = (uint8_t)((len >> 24) & 0xFF);

    int use_lazy = (ctx->level >= 4);

    size_t ip = 0;
    while (ip < len) {
        int match_len;
        size_t match_off;

        if (find_match(ctx, ip, in, len, &match_len, &match_off) &&
            /* 同 mz_lz77.c：偏移 0x200-0x3FF 的 match token 首字节为 0x81，
             * 与转义字面量标记冲突，解码器无法区分 → 数据损坏。此类匹配
             * 退化为字面量。 */
            !(match_off >= 0x200 && match_off <= 0x3FF)) {
            /* Lazy matching: for levels >= 4, check if the NEXT position
             * has a longer match. */
            if (use_lazy && ip + 1 < len && ip + 2 < len) {
                uint16_t h3 = SOLID_HASH3(in + ip);
                uint16_t idx = (uint16_t)(ip & SOLID_WMASK);
                ctx->chain[idx] = ctx->head[h3];
                ctx->head[h3] = (uint16_t)ip;

                int next_len;
                size_t next_off;
                if (find_match(ctx, ip + 1, in, len, &next_len, &next_off) &&
                    next_len > match_len + 1) {
                    goto emit_literal;
                }
            }

            /* v2 match token: 3 字节，16 位偏移，7 位长度 */
            if (match_len > SOLID_MAX_MATCH)
                match_len = SOLID_MAX_MATCH;
            out[op++] = (uint8_t)(0x80 | ((match_off >> 9) & 0x7F));
            out[op++] = (uint8_t)((match_off >> 1) & 0xFF);
            out[op++] = (uint8_t)(((match_off & 1) << 7)
                                  | ((match_len - SOLID_MIN_MATCH) & 0x7F));
            slide_and_insert(ctx, in, &ip, len, (size_t)match_len);
        } else {
emit_literal:
            /* Literal: 值 < 0x80 直接写入，否则用 0x81 转义 */
            if (in[ip] >= 0x80) {
                out[op++] = 0x81;
                out[op++] = in[ip];
            } else {
                out[op++] = in[ip];
            }
            slide_and_insert(ctx, in, &ip, len, 1);
        }
    }

    /* 结束标记：offset=0 的 match */
    out[op++] = 0x80;
    out[op++] = 0;
    out[op++] = 0;

    *out_len = op;
    return MZ_OK;
}

void mz_solid_finish(struct mz_solid_ctx *ctx)
{
    if (ctx) {
        ctx->active = 0;
        free(ctx);
    }
}
