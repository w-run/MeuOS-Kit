/* mxa_core.c — MxA (MeuOS Archive) container read/write
 *
 * Format (see FORMAT.md):
 *   [Archive Header (16B)] [File Data Blocks ...] [CD + Footer + Signature]
 *
 * Write: mxa_create -> mxa_add_file (xN) -> mxa_finish
 * Read:  mxa_open   -> mxa_read_file / mxa_list_files -> mxa_close
 */

#include "mxa.h"
#include "mz.h"
#include "mz_ed25519.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ================================================================
 * Format constants
 * ================================================================ */
#define MXA_MAGIC    0x3141784D  /* "MxA1" little-endian */
#define MXA_CD_MAGIC 0x4443784D  /* "MxCD" */
#define MXA_CF_MAGIC 0x4643784D  /* "MxCF" */
#define MXA_VERSION_CUR 1
#define MXA_VERSION_MIN 1

#define MXA_HDR_LEN     16
#define MXA_CD_HDR_LEN  8
#define MXA_CD_ENTRY_BASE 48  /* struct mxa_cd_entry without filename */
#define MXA_CD_FOOTER_LEN 40
#define MXA_SIG_LEN     64

/* ================================================================
 * Internal helpers – portable LE read/write
 * ================================================================ */
static void w16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}
static void w32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}
static void w64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) {
        p[i] = (uint8_t)(v & 0xFF);
        v >>= 8;
    }
}
static uint16_t r16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t r32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t r64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}

/* ================================================================
 * CRC32
 * ================================================================ */
static uint32_t crc32_bytes(const void *data, size_t len) {
    static const uint32_t table[256] = {
        0x00000000,0x77073096,0xEE0E612C,0x990951BA,
        0x076DC419,0x706AF48F,0xE963A535,0x9E6495A3,
        0x0EDB8832,0x79DCB8A4,0xE0D5E91E,0x97D2D988,
        0x09B64C2B,0x7EB17CBD,0xE7B82D07,0x90BF1D91,
        0x1DB71064,0x6AB020F2,0xF3B97148,0x84BE41DE,
        0x1ADAD47D,0x6DDDE4EB,0xF4D4B551,0x83D385C7,
        0x136C9856,0x646BA8C0,0xFD62F97A,0x8A65C9EC,
        0x14015C4F,0x63066CD9,0xFA0F3D63,0x8D080DF5,
        0x3B6E20C8,0x4C69105E,0xD56041E4,0xA2677172,
        0x3C03E4D1,0x4B04D447,0xD20D85FD,0xA50AB56B,
        0x35B5A8FA,0x42B2986C,0xDBBBC9D6,0xACBCF940,
        0x32D86CE3,0x45DF5C75,0xDCD60DCF,0xABD13D59,
        0x26D930AC,0x51DE003A,0xC8D75180,0xBFD06116,
        0x21B4F4B5,0x56B3C423,0xCFBA9599,0xB8BDA50F,
        0x2802B89E,0x5F058808,0xC60CD9B2,0xB10BE924,
        0x2F6F7C87,0x58684C11,0xC1611DAB,0xB6662D3D,
        0x76DC4190,0x01DB7106,0x98D220BC,0xEFD5102A,
        0x71B18589,0x06B6B51F,0x9FBFE4A5,0xE8B8D433,
        0x7807C9A2,0x0F00F934,0x9609A88E,0xE10E9818,
        0x7F6A0DBB,0x086D3D2D,0x91646C97,0xE6635C01,
        0x6B6B51F4,0x1C6C6162,0x856530D8,0xF262004E,
        0x6C0695ED,0x1B01A57B,0x8208F4C1,0xF50FC457,
        0x65B0D9C6,0x12B7E950,0x8BBEB8EA,0xFCB9887C,
        0x62DD1DDF,0x15DA2D49,0x8CD37CF3,0xFBD44C65,
        0x4DB26158,0x3AB551CE,0xA3BC0074,0xD4BB30E2,
        0x4ADFA541,0x3DD895D7,0xA4D1C46D,0xD3D6F4FB,
        0x4369E96A,0x346ED9FC,0xAD678846,0xDA60B8D0,
        0x44042D73,0x33031DE5,0xAA0A4C5F,0xDD0D7CC9,
        0x5005713C,0x270241AA,0xBE0B1010,0xC90C2086,
        0x5768B525,0x206F85B3,0xB966D409,0xCE61E49F,
        0x5EDEF90E,0x29D9C998,0xB0D09822,0xC7D7A8B4,
        0x59B33D17,0x2EB40D81,0xB7BD5C3B,0xC0BA6CAD,
        0xEDB88320,0x9ABFB3B6,0x03B6E20C,0x74B1D29A,
        0xEAD54739,0x9DD277AF,0x04DB2615,0x73DC1683,
        0xE3630B12,0x94643B84,0x0D6D6A3E,0x7A6A5AA8,
        0xE40ECF0B,0x9309FF9D,0x0A00AE27,0x7D079EB1,
        0xF00F9344,0x8708A3D2,0x1E01F268,0x6906C2FE,
        0xF762575D,0x806567CB,0x196C3671,0x6E6B06E7,
        0xFED41B76,0x89D32BE0,0x10DA7A5A,0x67DD4ACC,
        0xF9B9DF6F,0x8EBEEFF9,0x17B7BE43,0x60B08ED5,
        0xD6D6A3E8,0xA1D1937E,0x38D8C2C4,0x4FDFF252,
        0xD1BB67F1,0xA6BC5767,0x3FB506DD,0x48B2364B,
        0xD80D2BDA,0xAF0A1B4C,0x36034AF6,0x41047A60,
        0xDF60EFC3,0xA867DF55,0x316E8EEF,0x4669BE79,
        0xCB61B38C,0xBC66831A,0x256FD2A0,0x5268E236,
        0xCC0C7795,0xBB0B4703,0x220216B9,0x5505262F,
        0xC5BA3BBE,0xB2BD0B28,0x2BB45A92,0x5CB36A04,
        0xC2D7FFA7,0xB5D0CF31,0x2CD99E8B,0x5BDEAE1D,
        0x9B64C2B0,0xEC63F226,0x756AA39C,0x026D930A,
        0x9C0906A9,0xEB0E363F,0x72076785,0x05005713,
        0x95BF4A82,0xE2B87A14,0x7BB12BAE,0x0CB61B38,
        0x92D28E9B,0xE5D5BE0D,0x7CDCEFB7,0x0BDBDF21,
        0x86D3D2D4,0xF1D4E242,0x68DDB3F8,0x1FDA836E,
        0x81BE16CD,0xF6B9265B,0x6FB077E1,0x18B74777,
        0x88085AE6,0xFF0F6A70,0x66063BCA,0x11010B5C,
        0x8F659EFF,0xF862AE69,0x616BFFD3,0x166CCF45,
        0xA00AE278,0xD70DD2EE,0x4E048354,0x3903B3C2,
        0xA7672661,0xD06016F7,0x4969474D,0x3E6E77DB,
        0xAED16A4A,0xD9D65ADC,0x40DF0B66,0x37D83BF0,
        0xA9BCAE53,0xDEBB9EC5,0x47B2CF7F,0x30B5FFE9,
        0xBDBDF21C,0xCABAC28A,0x53B39330,0x24B4A3A6,
        0xBAD03605,0xCDD70693,0x54DE5729,0x23D967BF,
        0xB3667A2E,0xC4614AB8,0x5D681B02,0x2A6F2B94,
        0xB40BBE37,0xC30C8EA1,0x5A05DF1B,0x2D02EF8D,
    };
    uint32_t c = 0xFFFFFFFF;
    const uint8_t *u = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++)
        c = table[(c ^ u[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFF;
}

/* ================================================================
 * strdup polyfill
 * ================================================================ */
static char *local_strdup(const char *s) {
    size_t n = strlen(s);
    char *p = (char *)malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}

/* ================================================================
 * Internal structures
 * ================================================================ */

/* Write context */
struct mxa_write_ctx {
    uint8_t  *buf;
    size_t    buf_len;
    size_t    buf_cap;

    uint64_t  total_uncompressed;
    uint64_t  total_compressed;

    int       flags;
    int       align;
    int       level;

    /* Pending file entries */
    struct mxa_file_pending {
        char    *name;
        size_t   name_len;
        uint16_t mode;
        uint32_t uid, gid;
        uint64_t mtime;
        uint64_t offset;
        uint64_t size;
        uint64_t csize;
        int      codec;
    } *files;
    int num_files;
    int max_files;

    /* Crypto */
    uint8_t  key[32];
    uint8_t  sk[32];
    int      has_key;
    int      has_sk;
    int      next_file_id; /* for ChaCha20 nonce */
};

/* Read context */
struct mxa_read_ctx {
    const uint8_t *data;
    size_t         len;
    int            flags;

    struct mxa_file_entry *files;
    int num_files;

    uint64_t cd_start;
    uint8_t  key[32];     /* ChaCha20 key for decryption */
    int      has_key;
};

/* ================================================================
 * Buffer helpers
 * ================================================================ */
static int buf_grow(struct mxa_write_ctx *ctx, size_t need) {
    if (need <= ctx->buf_cap)
        return MXA_OK;
    size_t new_cap = ctx->buf_cap ? ctx->buf_cap * 2 : 4096;
    while (new_cap < need)
        new_cap *= 2;
    uint8_t *p = (uint8_t *)realloc(ctx->buf, new_cap);
    if (!p) return MXA_ERR_MEMORY;
    ctx->buf = p;
    ctx->buf_cap = new_cap;
    return MXA_OK;
}

static int buf_append(struct mxa_write_ctx *ctx, const void *data, size_t len) {
    int ret = buf_grow(ctx, ctx->buf_len + len);
    if (ret != MXA_OK) return ret;
    memcpy(ctx->buf + ctx->buf_len, data, len);
    ctx->buf_len += len;
    return MXA_OK;
}

static int buf_align(struct mxa_write_ctx *ctx) {
    if (ctx->align == 0) return MXA_OK;
    size_t mask = ((size_t)1 << ctx->align) - 1;
    size_t rem = ctx->buf_len & mask;
    if (rem == 0) return MXA_OK;
    size_t pad = ((size_t)1 << ctx->align) - rem;
    static const uint8_t zero[64] = {0};
    while (pad > 0) {
        size_t chunk = pad < sizeof(zero) ? pad : sizeof(zero);
        int ret = buf_append(ctx, zero, chunk);
        if (ret != MXA_OK) return ret;
        pad -= chunk;
    }
    return MXA_OK;
}

/* ================================================================
 * mxa_create
 * ================================================================ */
int mxa_create(void **out, const struct mxa_params *params) {
    if (!out || !params)
        return MXA_ERR_PARAM;
    if (params->flags & ~(MXA_FLAG_SIGNED | MXA_FLAG_ENCRYPTED))
        return MXA_ERR_PARAM;
    if (params->align < 0 || params->align > 12)
        return MXA_ERR_PARAM;
    if (params->level < 1 || params->level > 9)
        return MXA_ERR_PARAM;

    struct mxa_write_ctx *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return MXA_ERR_MEMORY;

    ctx->flags = params->flags;
    ctx->align = params->align;
    ctx->level = params->level;

    ctx->buf_cap = 4096;
    ctx->buf = malloc(ctx->buf_cap);
    if (!ctx->buf) { free(ctx); return MXA_ERR_MEMORY; }

    memset(ctx->buf, 0, MXA_HDR_LEN);
    ctx->buf_len = MXA_HDR_LEN;

    if (params->key)  { memcpy(ctx->key, params->key, 32);  ctx->has_key = 1; }
    if (params->sk)   { memcpy(ctx->sk, params->sk, 32);    ctx->has_sk  = 1; }

    *out = ctx;
    return MXA_OK;
}

/* ================================================================
 * mxa_add_file
 * ================================================================ */
int mxa_add_file(void *ctx, const char *name,
                 const void *data, size_t size,
                 uint16_t mode, uint32_t uid, uint32_t gid, uint64_t mtime) {
    if (!ctx || !name || (!data && size > 0))
        return MXA_ERR_PARAM;

    struct mxa_write_ctx *w = (struct mxa_write_ctx *)ctx;

    if (w->num_files >= w->max_files) {
        int new_max = w->max_files ? w->max_files * 2 : 16;
        void *p = realloc(w->files,
                          (size_t)new_max * sizeof(w->files[0]));
        if (!p) return MXA_ERR_MEMORY;
        w->files = (struct mxa_file_pending *)p;
        w->max_files = new_max;
    }

    size_t name_len = strlen(name);
    if (name_len > 0xFFFF) return MXA_ERR_PARAM;

    uint64_t data_offset = (uint64_t)w->buf_len;
    int ret;
    uint64_t csize;
    int codec = MXA_CODEC_MEUOS;

    if (size > 0) {
        size_t max_comp = mz_max_compressed_size(size, MZ_CODEC_MEUOS);
        void *comp = malloc(max_comp);
        if (!comp) return MXA_ERR_MEMORY;

        size_t comp_out = 0;
        int cr = mz_compress(data, size, &comp, &comp_out, MZ_CODEC_MEUOS, w->level);
        if (cr <= 0) {
            free(comp);
            ret = buf_append(w, data, size);
            if (ret != MXA_OK) return ret;
            csize = size;
            codec = MXA_CODEC_STORED;
        } else {
            csize = (uint64_t)comp_out;
            ret = buf_append(w, comp, comp_out);
            free(comp);
            if (ret != MXA_OK) return ret;
        }
    } else {
        csize = 0;
        codec = MXA_CODEC_STORED;
    }

    /* Encrypt stored/compressed data if MXA_FLAG_ENCRYPTED */
    if (w->has_key && (w->flags & MXA_FLAG_ENCRYPTED)) {
        uint8_t nonce[12];
        memset(nonce, 0, 12);
        uint64_t fid = data_offset;
        for (int b = 0; b < 8; b++) nonce[b] = (uint8_t)(fid >> (b * 8));
        uint8_t *enc_target = w->buf + data_offset;
        mz_chacha20(w->key, nonce, 0, enc_target, enc_target, (size_t)csize);
    }

    ret = buf_align(w);
    if (ret != MXA_OK) return ret;

    struct mxa_file_pending *e = &w->files[w->num_files];
    e->name = local_strdup(name);
    if (!e->name) return MXA_ERR_MEMORY;
    e->name_len = name_len;
    e->mode = mode;
    e->uid = uid;
    e->gid = gid;
    e->mtime = mtime;
    e->offset = data_offset;
    e->size = (uint64_t)size;
    e->csize = csize;
    e->codec = codec;
    w->num_files++;
    w->total_uncompressed += size;
    w->total_compressed += csize;

    return MXA_OK;
}

/* ================================================================
 * mxa_finish
 * ================================================================ */
int mxa_finish(void *ctx, void **result, size_t *result_len) {
    if (!ctx || !result || !result_len)
        return MXA_ERR_PARAM;

    struct mxa_write_ctx *w = (struct mxa_write_ctx *)ctx;

    uint64_t cd_start = (uint64_t)w->buf_len;
    size_t cd_total = MXA_CD_HDR_LEN;
    for (int i = 0; i < w->num_files; i++)
        cd_total += MXA_CD_ENTRY_BASE + w->files[i].name_len;
    cd_total += MXA_CD_FOOTER_LEN;
    if (w->has_sk) cd_total += MXA_SIG_LEN;

    int ret = buf_grow(w, w->buf_len + cd_total);
    if (ret != MXA_OK) return ret;

    /* CD Header */
    w32(w->buf + w->buf_len, MXA_CD_MAGIC);
    w32(w->buf + w->buf_len + 4, (uint32_t)w->num_files);
    w->buf_len += MXA_CD_HDR_LEN;

    /* CD Entries */
    size_t cd_entries_start = w->buf_len;
    for (int i = 0; i < w->num_files; i++) {
        struct mxa_file_pending *e = &w->files[i];
        uint8_t ent[MXA_CD_ENTRY_BASE];
        memset(ent, 0, sizeof(ent));
        w16(ent, (uint16_t)e->name_len);
        w16(ent + 2, e->mode);
        w32(ent + 4, e->uid);
        w32(ent + 8, e->gid);
        w64(ent + 12, e->mtime);
        w64(ent + 20, e->offset);
        w64(ent + 28, e->size);
        w64(ent + 36, e->csize);
        ent[44] = (uint8_t)e->codec;

        memcpy(w->buf + w->buf_len, ent, MXA_CD_ENTRY_BASE);
        w->buf_len += MXA_CD_ENTRY_BASE;
        memcpy(w->buf + w->buf_len, e->name, e->name_len);
        w->buf_len += e->name_len;
    }

    uint32_t cd_crc = crc32_bytes(w->buf + cd_entries_start,
                                  w->buf_len - cd_entries_start);

    /* CD Footer */
    uint8_t footer[MXA_CD_FOOTER_LEN];
    memset(footer, 0, sizeof(footer));
    w32(footer, MXA_CF_MAGIC);
    w32(footer + 4, (uint32_t)w->num_files);
    w64(footer + 8, cd_start);
    w64(footer + 16, w->total_uncompressed);
    w64(footer + 24, w->total_compressed);
    w32(footer + 32, cd_crc);
    /* Fill Archive Header (before footer checksum) */
    w32(w->buf, MXA_MAGIC);
    w16(w->buf + 4, (uint16_t)w->flags);
    w->buf[6] = MXA_VERSION_MIN;
    w->buf[7] = MXA_VERSION_CUR;
    w->buf[8] = (uint8_t)w->align;

    /* Recompute hdr_checksum with real header values */
    w32(footer + 36, crc32_bytes(w->buf, MXA_HDR_LEN));
    memcpy(w->buf + w->buf_len, footer, MXA_CD_FOOTER_LEN);
    w->buf_len += MXA_CD_FOOTER_LEN;

    /* Signature: compute over full archive, append at end */
    if (w->has_sk) {
        uint8_t sk64[MZ_ED25519_SK_LEN];
        uint8_t pk32[MZ_ED25519_PK_LEN];
        if (mz_ed25519_keypair(w->sk, sk64, pk32) == 0) {
            uint8_t sig[MXA_SIG_LEN];
            mz_ed25519_sign(sk64, w->buf, w->buf_len, sig);
            int ret = buf_append(w, sig, MXA_SIG_LEN);
            if (ret != MXA_OK) return ret;
            memset(sk64, 0, sizeof(sk64)); /* wipe key */
        } else {
            /* libsodium not available -- write placeholder */
            int ret = buf_grow(w, w->buf_len + MXA_SIG_LEN);
            if (ret != MXA_OK) return ret;
            memset(w->buf + w->buf_len, 0, MXA_SIG_LEN);
            w->buf_len += MXA_SIG_LEN;
        }
    }

    void *out = malloc(w->buf_len);
    if (!out) return MXA_ERR_MEMORY;
    memcpy(out, w->buf, w->buf_len);
    *result = out;
    *result_len = w->buf_len;

    return MXA_OK;
}

/* ================================================================
 * mxa_open
 * ================================================================ */
int mxa_open(const void *data, size_t len, void **ctx) {
    if (!data || !ctx || len < MXA_HDR_LEN)
        return MXA_ERR_PARAM;

    const uint8_t *p = (const uint8_t *)data;
    if (r32(p) != MXA_MAGIC)
        return MXA_ERR_DATA;

    uint8_t v_cur = p[7], v_min = p[6];
    if (v_cur > MXA_VERSION_CUR || v_min > MXA_VERSION_CUR)
        return MXA_ERR_DATA;

    /* Scan backwards for CD Footer */
    size_t scan_start = (len > MXA_CD_FOOTER_LEN + MXA_SIG_LEN)
                        ? len - MXA_CD_FOOTER_LEN - MXA_SIG_LEN
                        : MXA_HDR_LEN;
    if (scan_start + MXA_CD_FOOTER_LEN > len)
        return MXA_ERR_DATA;

    if (r32(p + scan_start) != MXA_CF_MAGIC) {
        scan_start = (len > MXA_CD_FOOTER_LEN)
                     ? len - MXA_CD_FOOTER_LEN : MXA_HDR_LEN;
        if (scan_start + MXA_CD_FOOTER_LEN > len || r32(p + scan_start) != MXA_CF_MAGIC)
            return MXA_ERR_DATA;
    }

    uint64_t cd_start = r64(p + scan_start + 8);
    uint32_t total_files = r32(p + scan_start + 4);

    if (r32(p + scan_start + 36) != crc32_bytes(p, MXA_HDR_LEN))
        return MXA_ERR_DATA;

    if (cd_start + MXA_CD_HDR_LEN > len || r32(p + cd_start) != MXA_CD_MAGIC)
        return MXA_ERR_DATA;
    if (r32(p + cd_start + 4) != total_files)
        return MXA_ERR_DATA;

    struct mxa_read_ctx *r = calloc(1, sizeof(*r));
    if (!r) return MXA_ERR_MEMORY;
    r->data = p;
    r->len = len;
    r->flags = r16(p + 4);
    r->cd_start = cd_start;

    /* Verify CD entries CRC32 (entries only, from CD header end to footer start) */
    if (total_files > 0) {
        size_t cd_entries_off = (size_t)(cd_start + MXA_CD_HDR_LEN);
        size_t cd_entries_sz  = (size_t)(scan_start - cd_entries_off);
        uint32_t stored_crc   = r32(p + scan_start + 32);
        uint32_t calc_crc     = crc32_bytes(p + cd_entries_off, cd_entries_sz);
        if (stored_crc != calc_crc)
            { free(r->files); free(r); return MXA_ERR_DATA; }
    }

    size_t cd_pos = cd_start + MXA_CD_HDR_LEN;
    r->files = calloc((size_t)total_files, sizeof(struct mxa_file_entry));
    if (!r->files && total_files > 0) { free(r); return MXA_ERR_MEMORY; }

    for (uint32_t i = 0; i < total_files; i++) {
        if (cd_pos + MXA_CD_ENTRY_BASE > len)
            { mxa_close(r); return MXA_ERR_DATA; }
        uint16_t name_len = r16(p + cd_pos);
        if (cd_pos + MXA_CD_ENTRY_BASE + name_len > len)
            { mxa_close(r); return MXA_ERR_DATA; }

        struct mxa_file_entry *e = &r->files[i];
        e->name_len = name_len;
        e->name = malloc(name_len + 1);
        if (!e->name) { mxa_close(r); return MXA_ERR_MEMORY; }
        memcpy(e->name, p + cd_pos + MXA_CD_ENTRY_BASE, name_len);
        e->name[name_len] = '\0';
        e->mode = r16(p + cd_pos + 2);
        e->uid = r32(p + cd_pos + 4);
        e->gid = r32(p + cd_pos + 8);
        e->mtime = r64(p + cd_pos + 12);
        e->offset = r64(p + cd_pos + 20);
        e->size = r64(p + cd_pos + 28);
        e->csize = r64(p + cd_pos + 36);
        e->codec = (int)p[cd_pos + 44];
        cd_pos += MXA_CD_ENTRY_BASE + name_len;
        r->num_files++;
    }

    *ctx = r;
    return MXA_OK;
}

/* ================================================================
 * mxa_read_file
 * ================================================================ */
int mxa_read_file(void *ctx, const char *name,
                  void **data, size_t *size) {
    if (!ctx || !name || !data || !size)
        return MXA_ERR_PARAM;

    struct mxa_read_ctx *r = (struct mxa_read_ctx *)ctx;

    struct mxa_file_entry *entry = NULL;
    for (int i = 0; i < r->num_files; i++) {
        if (strcmp(r->files[i].name, name) == 0) {
            entry = &r->files[i];
            break;
        }
    }
    if (!entry) return MXA_ERR_NOTFOUND;
    if (entry->offset + entry->csize > r->len)
        return MXA_ERR_DATA;

    const uint8_t *blob = r->data + entry->offset;

    if (entry->codec == MXA_CODEC_STORED) {
        size_t out_sz = (size_t)(entry->size > 0 ? entry->size : 1);
        void *out = malloc(out_sz);
        if (!out) return MXA_ERR_MEMORY;

        if (r->has_key && (r->flags & MXA_FLAG_ENCRYPTED)) {
            /* Decrypt raw stored data */
            uint8_t nonce[12];
            memset(nonce, 0, 12);
            uint64_t fid = entry->offset;
            for (int b = 0; b < 8; b++) nonce[b] = (uint8_t)(fid >> (b * 8));
            mz_chacha20(r->key, nonce, 0, blob, out, (size_t)entry->csize);
        } else {
            memcpy(out, blob, (size_t)entry->size);
        }

        *data = out;
        *size = (size_t)entry->size;
        return MXA_OK;
    }

    if (entry->codec == MXA_CODEC_MEUOS) {
        const uint8_t *decrypt_src = blob;
        size_t decrypt_sz = (size_t)entry->csize;
        uint8_t *decrypt_buf = NULL;

        /* Decrypt if archive is encrypted */
        if (r->has_key && (r->flags & MXA_FLAG_ENCRYPTED)) {
            decrypt_buf = malloc(decrypt_sz);
            if (!decrypt_buf) return MXA_ERR_MEMORY;
            uint8_t nonce[12];
            memset(nonce, 0, 12);
            /* File ID from entry offset (deterministic) */
            uint64_t fid = entry->offset;
            for (int b = 0; b < 8; b++) nonce[b] = (uint8_t)(fid >> (b * 8));
            mz_chacha20(r->key, nonce, 0, blob, decrypt_buf, decrypt_sz);
            decrypt_src = decrypt_buf;
        }

        void *decomp = NULL;
        size_t decomp_len = 0;
        int ret = mz_decompress(decrypt_src, decrypt_sz,
                                &decomp, &decomp_len, MZ_CODEC_MEUOS);
        free(decrypt_buf);
        if (ret <= 0) return MXA_ERR_DATA;
        if (decomp_len != entry->size) return MXA_ERR_DATA;
        *data = decomp;
        *size = decomp_len;
        return MXA_OK;
    }

    return MXA_ERR_DATA;
}

/* ================================================================
 * mxa_list_files
 * ================================================================ */
int mxa_list_files(void *ctx, struct mxa_file_entry **entries, int *count) {
    if (!ctx || !entries || !count)
        return MXA_ERR_PARAM;

    struct mxa_read_ctx *r = (struct mxa_read_ctx *)ctx;
    *count = r->num_files;
    if (r->num_files == 0) { *entries = NULL; return MXA_OK; }

    struct mxa_file_entry *dup = calloc(
        (size_t)r->num_files, sizeof(struct mxa_file_entry));
    if (!dup) return MXA_ERR_MEMORY;

    for (int i = 0; i < r->num_files; i++) {
        dup[i] = r->files[i];
        dup[i].name = local_strdup(r->files[i].name);
        if (!dup[i].name) {
            for (int j = 0; j < i; j++) free(dup[j].name);
            free(dup); return MXA_ERR_MEMORY;
        }
    }

    *entries = dup;
    return MXA_OK;
}

/* ================================================================
 * mxa_verify -- verify Ed25519 signature of archived content
 * ================================================================ */
int mxa_set_key(void *ctx, const uint8_t key[32]) {
    if (!ctx || !key) return MXA_ERR_PARAM;
    struct mxa_read_ctx *r = (struct mxa_read_ctx *)ctx;
    memcpy(r->key, key, 32);
    r->has_key = 1;
    return MXA_OK;
}

int mxa_verify(void *ctx, const uint8_t public_key[32]) {
    if (!ctx || !public_key) return MXA_ERR_PARAM;
    struct mxa_read_ctx *r = (struct mxa_read_ctx *)ctx;
    if (!(r->flags & MXA_FLAG_SIGNED)) return MXA_ERR_DATA;
    if (r->len < MXA_SIG_LEN) return MXA_ERR_DATA;
    int ok = mz_verify_block(r->data, r->len, public_key);
    return ok ? MXA_OK : MXA_ERR_CRYPT;
}

/* ================================================================
 * mxa_close
 * ================================================================ */
void mxa_close(void *ctx) {
    if (!ctx) return;
    struct mxa_read_ctx *r = (struct mxa_read_ctx *)ctx;
    for (int i = 0; i < r->num_files; i++)
        free(r->files[i].name);
    free(r->files);
    free(r);
}

/* ================================================================
 * mxa_strerror
 * ================================================================ */
const char *mxa_strerror(int err) {
    switch (err) {
    case MXA_OK:          return "Ok";
    case MXA_ERR_MEMORY:  return "Out of memory";
    case MXA_ERR_DATA:    return "Corrupt or invalid archive";
    case MXA_ERR_PARAM:   return "Invalid parameter";
    case MXA_ERR_NOTFOUND:return "File not found";
    case MXA_ERR_CRYPT:   return "Cryptographic error";
    default:              return "Unknown error";
    }
}
