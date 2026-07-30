#include "mz.h"
#include <stdlib.h>
#include <string.h>

/* strdup is POSIX, not C11 — implement a portable version */
static char *local_strdup(const char *s)
{
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    if (p)
        memcpy(p, s, n + 1);
    return p;
}

/* ================================================================
 * Internal structures
 * ================================================================ */

/* Magic + Header: MZv2(4B) | version(1B) | level(1B) | flags(2B) | reserved(4B) */
#define MZ2_MAGIC       "MZv2"
#define MZ2_VERSION     2
#define MZ2_HEADER_LEN  12

/* Block header: type(1B) | size_be(3B) */
#define MZ2_BLOCK_HDR_LEN 4
#define MZ2_MAX_BLOCK_SIZE 0x00FFFFFFu

/* Write context */
struct mz_write_ctx {
    uint8_t *buf;
    size_t buf_len;
    size_t buf_cap;

    struct mz_file_entry *files;
    int num_files;
    int max_files;

    int level;
    int flags;
};

/* Read context */
struct mz_read_ctx {
    const uint8_t *data;
    size_t len;
    int level;
    int flags;

    struct mz_file_entry *files;
    int num_files;
};

/* ================================================================
 * Internal helpers
 * ================================================================ */

static void write_be24(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)((v >> 16) & 0xFF);
    dst[1] = (uint8_t)((v >> 8) & 0xFF);
    dst[2] = (uint8_t)(v & 0xFF);
}

static uint32_t read_be24(const uint8_t *src)
{
    return ((uint32_t)src[0] << 16)
         | ((uint32_t)src[1] << 8)
         | (uint32_t)src[2];
}

static void write_le32(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v & 0xFF);
    dst[1] = (uint8_t)((v >> 8) & 0xFF);
    dst[2] = (uint8_t)((v >> 16) & 0xFF);
    dst[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint32_t read_le32(const uint8_t *src)
{
    return (uint32_t)src[0]
         | ((uint32_t)src[1] << 8)
         | ((uint32_t)src[2] << 16)
         | ((uint32_t)src[3] << 24);
}

static void write_le16(uint8_t *dst, uint16_t v)
{
    dst[0] = (uint8_t)(v & 0xFF);
    dst[1] = (uint8_t)((v >> 8) & 0xFF);
}

static uint16_t read_le16(const uint8_t *src)
{
    return (uint16_t)src[0]
         | ((uint16_t)src[1] << 8);
}

/* Grow buffer to hold at least `need` bytes */
static int buf_grow(uint8_t **buf, size_t *cap, size_t need)
{
    if (need <= *cap)
        return MZ_OK;
    size_t new_cap = *cap ? *cap * 2 : 4096;
    while (new_cap < need)
        new_cap *= 2;
    uint8_t *p = realloc(*buf, new_cap);
    if (!p)
        return MZ_ERR_MEMORY;
    *buf = p;
    *cap = new_cap;
    return MZ_OK;
}

static int buf_append(struct mz_write_ctx *ctx, const void *data, size_t len)
{
    int ret = buf_grow(&ctx->buf, &ctx->buf_cap, ctx->buf_len + len);
    if (ret != MZ_OK)
        return ret;
    memcpy(ctx->buf + ctx->buf_len, data, len);
    ctx->buf_len += len;
    return MZ_OK;
}

static int append_block(struct mz_write_ctx *ctx, uint8_t type,
                        const void *data, size_t size)
{
    uint8_t hdr[MZ2_BLOCK_HDR_LEN];
    hdr[0] = type;
    write_be24(hdr + 1, (uint32_t)size);

    int ret = buf_append(ctx, hdr, MZ2_BLOCK_HDR_LEN);
    if (ret != MZ_OK)
        return ret;
    return buf_append(ctx, data, size);
}

/* ================================================================
 * mz2_create — allocate write context, write MZv2 magic + header
 * ================================================================ */
int mz2_create(void **out, size_t *out_len, const struct mz_params *params)
{
    if (!params || params->level < 1 || params->level > 9)
        return MZ_ERR_PARAM;
    if (params->flags & ~(MZ_FLAG_SOLID | MZ_FLAG_ENCRYPT | MZ_FLAG_SIGNED))
        return MZ_ERR_PARAM;

    struct mz_write_ctx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx)
        return MZ_ERR_MEMORY;

    ctx->level = params->level;
    ctx->flags = params->flags;

    ctx->buf_cap = 4096;
    ctx->buf = malloc(ctx->buf_cap);
    if (!ctx->buf) {
        free(ctx);
        return MZ_ERR_MEMORY;
    }

    /* Write Magic + Header */
    memcpy(ctx->buf, MZ2_MAGIC, 4);
    ctx->buf[4] = MZ2_VERSION;
    ctx->buf[5] = (uint8_t)params->level;
    write_le16(ctx->buf + 6, (uint16_t)params->flags);
    ctx->buf[8]  = 0; /* reserved */
    ctx->buf[9]  = 0;
    ctx->buf[10] = 0;
    ctx->buf[11] = 0;
    ctx->buf_len = MZ2_HEADER_LEN;

    if (out)
        *out = ctx;
    if (out_len)
        *out_len = 0;
    return MZ_OK;
}

/* ================================================================
 * mz2_add_file — compress and append file data as a block
 * ================================================================ */
int mz2_add_file(void *ctx, const char *name, const void *data,
                 size_t size, uint16_t mode)
{
    if (!ctx || !name || (!data && size > 0))
        return MZ_ERR_PARAM;

    struct mz_write_ctx *w = (struct mz_write_ctx *)ctx;

    if (w->num_files >= w->max_files) {
        int new_max = w->max_files ? w->max_files * 2 : 16;
        struct mz_file_entry *f = realloc(
            w->files, (size_t)new_max * sizeof(struct mz_file_entry));
        if (!f)
            return MZ_ERR_MEMORY;
        w->files = f;
        w->max_files = new_max;
    }

    size_t name_len = strlen(name);
    if (name_len > 0xFFFF)
        return MZ_ERR_PARAM;

    uint32_t data_offset = (uint32_t)(w->buf_len - MZ2_HEADER_LEN);

    int ret = append_block(w, MZ_BLOCK_RAW, data, size);
    if (ret != MZ_OK)
        return ret;

    struct mz_file_entry *ent = &w->files[w->num_files];
    ent->name = local_strdup(name);
    if (!ent->name)
        return MZ_ERR_MEMORY;
    ent->name_len = (uint16_t)name_len;
    ent->uid = 0;
    ent->gid = 0;
    ent->mode = mode;
    ent->size = (uint32_t)size;
    ent->offset = data_offset;
    ent->csize = (uint32_t)size;
    w->num_files++;

    return MZ_OK;
}

/* ================================================================
 * mz2_finish — write file table block + optional signature, output result
 * ================================================================ */
int mz2_finish(void *ctx, void **result, size_t *result_len)
{
    if (!ctx || !result || !result_len)
        return MZ_ERR_PARAM;

    struct mz_write_ctx *w = (struct mz_write_ctx *)ctx;
    int ret;

    /* Build file table data:
     *   entry_count(4B LE) + entries[]
     * Each entry: name_len(2B LE) | name(variable) | uid(4B) | gid(4B)
     *             | mode(2B) | size(4B) | offset(4B) | csize(4B) */
    size_t ft_size = 4;
    int i;
    for (i = 0; i < w->num_files; i++) {
        ft_size += 2;                      /* name_len */
        ft_size += w->files[i].name_len;   /* name */
        ft_size += 4 + 4 + 2 + 4 + 4 + 4;  /* uid+gid+mode+size+offset+csize */
    }

    uint8_t *ft_data = malloc(ft_size);
    if (!ft_data)
        return MZ_ERR_MEMORY;

    size_t off = 0;
    write_le32(ft_data + off, (uint32_t)w->num_files);
    off += 4;

    for (i = 0; i < w->num_files; i++) {
        struct mz_file_entry *e = &w->files[i];
        write_le16(ft_data + off, e->name_len);
        off += 2;
        memcpy(ft_data + off, e->name, e->name_len);
        off += e->name_len;
        write_le32(ft_data + off, e->uid);
        off += 4;
        write_le32(ft_data + off, e->gid);
        off += 4;
        write_le16(ft_data + off, e->mode);
        off += 2;
        write_le32(ft_data + off, e->size);
        off += 4;
        write_le32(ft_data + off, e->offset);
        off += 4;
        write_le32(ft_data + off, e->csize);
        off += 4;
    }

    ret = append_block(w, MZ_BLOCK_RAW, ft_data, ft_size);
    free(ft_data);
    if (ret != MZ_OK)
        return ret;

    /* Optional SIGNED block placeholder */
    if (w->flags & MZ_FLAG_SIGNED) {
        uint8_t sig_data[32 + 64 + 2 + 1]; /* pk + sig + block_count + algo_flag */
        memset(sig_data, 0, sizeof(sig_data));
        ret = append_block(w, MZ_BLOCK_SIGNED, sig_data, sizeof(sig_data));
        if (ret != MZ_OK)
            return ret;
    }

    void *out = malloc(w->buf_len);
    if (!out)
        return MZ_ERR_MEMORY;
    memcpy(out, w->buf, w->buf_len);
    *result = out;
    *result_len = w->buf_len;

    return MZ_OK;
}

/* ================================================================
 * mz2_open — open mz2 file for reading, parse header and file table
 * ================================================================ */
int mz2_open(const void *data, size_t len, void **ctx)
{
    if (!data || !ctx || len < MZ2_HEADER_LEN)
        return MZ_ERR_PARAM;

    if (memcmp(data, MZ2_MAGIC, 4) != 0)
        return MZ_ERR_DATA;

    const uint8_t *p = (const uint8_t *)data;
    if (p[4] != MZ2_VERSION)
        return MZ_ERR_DATA;

    struct mz_read_ctx *r = calloc(1, sizeof(*r));
    if (!r)
        return MZ_ERR_MEMORY;

    r->data = p;
    r->len = len;
    r->level = p[5];
    r->flags = read_le16(p + 6);

    /* Walk block chain to find the file table.
     * File table is the last RAW block before optional SIGNED block. */
    size_t blk_off = MZ2_HEADER_LEN;
    size_t last_raw_data_off = 0;

    while (blk_off + MZ2_BLOCK_HDR_LEN <= len) {
        uint8_t blk_type = p[blk_off];
        uint32_t blk_sz = read_be24(p + blk_off + 1);
        size_t total = MZ2_BLOCK_HDR_LEN + blk_sz;

        if (blk_off + total > len)
            break;

        if (blk_type == MZ_BLOCK_RAW)
            last_raw_data_off = blk_off + MZ2_BLOCK_HDR_LEN;
        else if (blk_type == MZ_BLOCK_SIGNED)
            break;

        blk_off += total;
    }

    if (last_raw_data_off == 0) {
        free(r);
        return MZ_ERR_DATA;
    }

    /* Parse file table */
    size_t ft_avail = len - last_raw_data_off;
    const uint8_t *ft_data = r->data + last_raw_data_off;

    if (ft_avail < 4) {
        free(r);
        return MZ_ERR_DATA;
    }

    int num_files = (int)read_le32(ft_data);
    if (num_files < 0 || (size_t)num_files > ft_avail / 2) {
        free(r);
        return MZ_ERR_DATA;
    }

    r->files = calloc((size_t)num_files, sizeof(struct mz_file_entry));
    if (!r->files) {
        free(r);
        return MZ_ERR_MEMORY;
    }

    size_t pos = 4;
    int fi;
    for (fi = 0; fi < num_files; fi++) {
        struct mz_file_entry *e = &r->files[fi];

        if (pos + 2 > ft_avail) { mz2_close(r); return MZ_ERR_DATA; }
        e->name_len = read_le16(ft_data + pos);
        pos += 2;

        if (pos + e->name_len > ft_avail) { mz2_close(r); return MZ_ERR_DATA; }
        e->name = malloc((size_t)e->name_len + 1);
        if (!e->name) { mz2_close(r); return MZ_ERR_MEMORY; }
        memcpy(e->name, ft_data + pos, e->name_len);
        e->name[e->name_len] = '\0';
        pos += e->name_len;

        if (pos + 4 + 4 + 2 + 4 + 4 + 4 > ft_avail) { mz2_close(r); return MZ_ERR_DATA; }
        e->uid    = read_le32(ft_data + pos); pos += 4;
        e->gid    = read_le32(ft_data + pos); pos += 4;
        e->mode   = read_le16(ft_data + pos); pos += 2;
        e->size   = read_le32(ft_data + pos); pos += 4;
        e->offset = read_le32(ft_data + pos); pos += 4;
        e->csize  = read_le32(ft_data + pos); pos += 4;
    }

    r->num_files = num_files;
    *ctx = r;
    return MZ_OK;
}

/* ================================================================
 * mz2_read_file — find file by name and decompress
 * ================================================================ */
int mz2_read_file(void *ctx, const char *name, void **data, size_t *size)
{
    if (!ctx || !name || !data || !size)
        return MZ_ERR_PARAM;

    struct mz_read_ctx *r = (struct mz_read_ctx *)ctx;
    int i;

    for (i = 0; i < r->num_files; i++) {
        if (strlen(name) == (size_t)r->files[i].name_len &&
            memcmp(name, r->files[i].name, r->files[i].name_len) == 0)
            break;
    }
    if (i >= r->num_files)
        return MZ_ERR_DATA;

    struct mz_file_entry *e = &r->files[i];
    size_t raw_off = MZ2_HEADER_LEN + (size_t)e->offset;

    if (raw_off + MZ2_BLOCK_HDR_LEN > r->len)
        return MZ_ERR_DATA;

    const uint8_t *p = r->data + raw_off;
    uint8_t blk_type = p[0];
    uint32_t blk_sz = read_be24(p + 1);

    if (raw_off + MZ2_BLOCK_HDR_LEN + blk_sz > r->len)
        return MZ_ERR_DATA;
    if (blk_sz != e->csize)
        return MZ_ERR_DATA;
    if (blk_type != MZ_BLOCK_RAW)
        return MZ_ERR_CODEC;

    void *out = malloc(e->size);
    if (!out)
        return MZ_ERR_MEMORY;
    memcpy(out, p + MZ2_BLOCK_HDR_LEN, e->size);

    *data = out;
    *size = e->size;
    return MZ_OK;
}

/* ================================================================
 * mz2_close — free context
 * ================================================================ */
void mz2_close(void *ctx)
{
    if (!ctx)
        return;

    struct mz_read_ctx *r = (struct mz_read_ctx *)ctx;
    if (r->files) {
        int i;
        for (i = 0; i < r->num_files; i++)
            free(r->files[i].name);
        free(r->files);
    }
    free(r);
}

/* ================================================================
 * mz2_block_read — read next block header from offset
 * ================================================================ */
int mz2_block_read(const void *data, size_t len, size_t *offset,
                   uint8_t *type, size_t *blk_size)
{
    if (!data || !offset || !type || !blk_size)
        return MZ_ERR_PARAM;

    size_t off = *offset;
    if (off + MZ2_BLOCK_HDR_LEN > len)
        return MZ_ERR_STREAM;

    const uint8_t *p = (const uint8_t *)data + off;
    *type = p[0];
    *blk_size = read_be24(p + 1);
    *offset = off + MZ2_BLOCK_HDR_LEN + *blk_size;
    return MZ_OK;
}

/* ================================================================
 * mz2_block_encrypt — encrypt a block (stub, no real crypto)
 * ================================================================ */
int mz2_block_encrypt(uint8_t *block, size_t size,
                      const uint8_t key[32], const uint8_t nonce[12])
{
    (void)block;
    (void)size;
    (void)key;
    (void)nonce;
    return MZ_ERR_CRYPT;
}

/* ================================================================
 * mz2_block_sign — sign a block (stub, no real crypto)
 * ================================================================ */
int mz2_block_sign(uint8_t *block, size_t size,
                   const uint8_t secret_key[32])
{
    (void)block;
    (void)size;
    (void)secret_key;
    return MZ_ERR_CRYPT;
}

/* ================================================================
 * mz2_level_supported — check if level is in 1..9
 * ================================================================ */
int mz2_level_supported(int level)
{
    return (level >= 1 && level <= 9) ? 1 : 0;
}

/* ================================================================
 * Core API wrappers -- route codec calls to the LZ77 module
 * ================================================================ */
int mz_compress(const void *in, size_t il, void **r, size_t *rl, int c, int lv)
{
    if (c == MZ_CODEC_LZ77 || c == MZ_CODEC_LZ77_HUFF)
        return mz_compress_lz77(in, il, r, rl, lv);
    return MZ_ERR_CODEC;
}

int mz_decompress(const void *in, size_t il, void **r, size_t *rl, int c)
{
    if (c == MZ_CODEC_LZ77 || c == MZ_CODEC_LZ77_HUFF)
        return mz_decompress_lz77(in, il, r, rl);
    return MZ_ERR_CODEC;
}

size_t mz_max_compressed_size(size_t il, int c)
{
    (void)c;
    return mz_max_compressed_size_lz77(il);
}

const char *mz_strerror(int e)
{
    switch (e) {
    case MZ_OK:         return "no error";
    case MZ_ERR_MEMORY: return "out of memory";
    case MZ_ERR_DATA:   return "corrupt data";
    case MZ_ERR_PARAM:  return "invalid parameter";
    case MZ_ERR_STREAM: return "stream error";
    case MZ_ERR_CODEC:  return "unsupported codec";
    case MZ_ERR_CRYPT:  return "crypto error";
    default:            return "unknown error";
    }
}
