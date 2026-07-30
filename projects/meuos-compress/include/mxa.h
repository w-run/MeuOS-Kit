#ifndef MXA_H
#define MXA_H

#include <stddef.h>
#include <stdint.h>

/* Archive flags */
#define MXA_FLAG_SIGNED     0x0001
#define MXA_FLAG_ENCRYPTED  0x0002

/* Codec IDs */
#define MXA_CODEC_STORED    0
#define MXA_CODEC_MEUOS     1

/* Error codes */
#define MXA_OK          0
#define MXA_ERR_MEMORY  -1
#define MXA_ERR_DATA    -2
#define MXA_ERR_PARAM   -3
#define MXA_ERR_NOTFOUND -4
#define MXA_ERR_CRYPT   -5

/* File entry */
struct mxa_file_entry {
    char    *name;
    size_t   name_len;
    uint16_t mode;
    uint32_t uid, gid;
    uint64_t mtime;
    uint64_t offset;   /* data offset in archive */
    uint64_t size;     /* uncompressed size */
    uint64_t csize;    /* compressed size */
    int      codec;    /* MXA_CODEC_* */
};

/* Write parameters */
struct mxa_params {
    int    flags;       /* MXA_FLAG_* */
    int    align;       /* align_shift (0..12) */
    int    level;       /* compression level 1-9 */
    const uint8_t *key;  /* ChaCha20 key (32B) or NULL */
    const uint8_t *sk;   /* Ed25519 secret key (32B) or NULL */
};

/* === API === */

/* Write */
int  mxa_create(void **ctx, const struct mxa_params *params);
int  mxa_add_file(void *ctx, const char *name,
                  const void *data, size_t size,
                  uint16_t mode, uint32_t uid, uint32_t gid, uint64_t mtime);
int  mxa_finish(void *ctx, void **result, size_t *result_len);

/* Read */
int  mxa_open(const void *data, size_t len, void **ctx);
int  mxa_read_file(void *ctx, const char *name,
                   void **data, size_t *size);
int  mxa_list_files(void *ctx,
                    struct mxa_file_entry **entries, int *count);
void mxa_close(void *ctx);

/* Utility */
const char *mxa_strerror(int err);

#endif /* MXA_H */
