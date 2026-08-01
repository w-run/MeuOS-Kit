#ifndef MZ_H
#define MZ_H
#include <stddef.h>
#include <stdint.h>

/* Codecs — 对外统一使用 MZ_CODEC_MEUOS */
#define MZ_CODEC_AUTO      0   /* 解压时自动嗅探格式 */
#define MZ_CODEC_LZ77      1
#define MZ_CODEC_LZ77_HUFF 2
#define MZ_CODEC_MEUOS     3   /* meuos-compress 统一算法引擎 */

/* Compression levels */
#define MZ_LEVEL_FASTEST   1
#define MZ_LEVEL_BALANCED  6
#define MZ_LEVEL_MAXIMUM   9

/* Block types */
#define MZ_BLOCK_RAW          0
#define MZ_BLOCK_LZ77_HUFF    1
#define MZ_BLOCK_SOLID_START  2
#define MZ_BLOCK_SOLID_NEXT   3
#define MZ_BLOCK_ENCRYPTED    4
#define MZ_BLOCK_SIGNED       5

/* Flags */
#define MZ_FLAG_SOLID   0x0001
#define MZ_FLAG_ENCRYPT 0x0002
#define MZ_FLAG_SIGNED  0x0004

/* Error codes */
#define MZ_OK         0
#define MZ_ERR_MEMORY  -1
#define MZ_ERR_DATA    -2
#define MZ_ERR_PARAM   -3
#define MZ_ERR_STREAM  -4
#define MZ_ERR_CODEC   -5
#define MZ_ERR_CRYPT   -6

/* File entry metadata */
struct mz_file_entry {
    char *name;      /* filename (not null-terminated in storage) */
    uint16_t name_len;
    uint32_t uid, gid;
    uint16_t mode;
    uint32_t size;   /* uncompressed size */
    uint32_t offset; /* data offset in block chain */
    uint32_t csize;  /* compressed size in block chain */
};

/* Compression parameters (future: level-based selection) */
struct mz_params {
    int level;       /* 1-9 */
    int flags;       /* MZ_FLAG_* bitmask */
};

/* Codec-level API (internal -- one per codec) */
int mz_compress_lz77(const void *in, size_t il, void **r, size_t *rl, int lv);
int mz_decompress_lz77(const void *in, size_t il, void **r, size_t *rl);
size_t mz_max_compressed_size_lz77(size_t il);
/* Core API */
int mz_compress(const void *in, size_t il, void **r, size_t *rl, int c, int lv);
int mz_decompress(const void *in, size_t il, void **r, size_t *rl, int c);
size_t mz_max_compressed_size(size_t il, int c);
const char *mz_strerror(int e);

/* Low-level crypto */
void mz_chacha20(const uint8_t key[32], const uint8_t nonce[12],
                 uint64_t counter, const uint8_t *in, uint8_t *out, size_t len);
int mz_sign_block(uint8_t *block, size_t size, const uint8_t secret_key[32]);
int mz_verify_block(const uint8_t *block, size_t size, const uint8_t public_key[32]);
/* v2 API - container format */
int mz2_create(void **out, size_t *out_len, const struct mz_params *params);
int mz2_add_file(void *ctx, const char *name, const void *data, size_t size, uint16_t mode);
int mz2_finish(void *ctx, void **result, size_t *result_len);
int mz2_open(const void *data, size_t len, void **ctx);
int mz2_read_file(void *ctx, const char *name, void **data, size_t *size);
void mz2_close(void *ctx);

/* v2 API - streaming block I/O */
int mz2_block_read(const void *data, size_t len, size_t *offset, uint8_t *type, size_t *blk_size);
int mz2_block_encrypt(uint8_t *block, size_t size, const uint8_t key[32], const uint8_t nonce[12]);
int mz2_block_sign(uint8_t *block, size_t size, const uint8_t secret_key[32]);

/* v2 API - level query */
/* Huffman codec */
int mz_huf_compress(const unsigned char *in, size_t inlen,
                    unsigned char *out, size_t *outlen);
int mz_huf_decompress(const unsigned char *in, size_t inlen,
                      unsigned char *out, size_t *outlen);

int mz2_level_supported(int level);

/* tANS codec */
int mz_tans_compress(const unsigned char *in, size_t inlen,
                     unsigned char *out, size_t *outlen);
int mz_tans_decompress(const unsigned char *in, size_t inlen,
                       unsigned char *out, size_t *outlen);


/* Combo pipeline: LZ77 + entropy */
int mz_compress_meuos(const void *in, size_t il, void **r, size_t *rl, int lv);
int mz_decompress_meuos(const void *in, size_t il, void **r, size_t *rl);

/* Combo pipeline: LZ77 + entropy coding */
int mz_compress_meuos(const void *in, size_t il, void **r, size_t *rl, int lv);
int mz_decompress_meuos(const void *in, size_t il, void **r, size_t *rl);

/* Solid compression */
struct mz_solid_ctx;
int mz_solid_start(struct mz_solid_ctx **ctx, int level);
int mz_solid_add(struct mz_solid_ctx *ctx, const void *data, size_t len,
                 void *output, size_t *out_len);
void mz_solid_finish(struct mz_solid_ctx *ctx);

/* ===================================================================
 * Checksum / Hash — CRC-32 (IEEE 802.3) / Adler-32 (RFC 1950)
 * =================================================================== */

/* 一次性计算 */
uint32_t mz_crc32(const void *data, size_t len);
uint32_t mz_adler32(const void *data, size_t len);

/* 增量式 CRC-32 */
uint32_t mz_crc32_init(void);
uint32_t mz_crc32_update(uint32_t crc, const void *data, size_t len);
uint32_t mz_crc32_final(uint32_t crc);

/* 增量式 Adler-32 */
uint32_t mz_adler32_init(void);
uint32_t mz_adler32_update(uint32_t adler, const void *data, size_t len);
uint32_t mz_adler32_final(uint32_t adler);

#endif
