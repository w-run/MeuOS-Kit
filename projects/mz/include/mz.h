/* mz — MeuOS compression library
 *
 * Multi-codec compression/decompression API with streaming support.
 * Built-in codec: LZ77 + Huffman (level 1-9)
 * Designed for future extension: zstd, LZ4, etc.
 *
 * License: RFL v1.0
 */
#ifndef MEUOS_COMPRESS_H
#define MEUOS_COMPRESS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Codec identifiers --- */
#define MZ_CODEC_LZ77_HUF  1   /* Built-in LZ77 + Huffman */
#define MZ_CODEC_NONE      0   /* Passthrough (no compression) */

/* --- Error codes --- */
#define MZ_OK           0
#define MZ_ERR_MEMORY  -1
#define MZ_ERR_DATA    -2
#define MZ_ERR_PARAM   -3
#define MZ_ERR_STREAM  -4
#define MZ_ERR_CODEC   -5

/* --- Compression levels (codec-specific mapping) --- */
#define MZ_LEVEL_FAST   1   /* Fastest, lowest ratio */
#define MZ_LEVEL_NORMAL 5   /* Default balance */
#define MZ_LEVEL_MAX    9   /* Slowest, highest ratio */

/* --- Stream state --- */
struct mz_stream {
    void *internal;          /* per-codec private state */
    int codec;               /* MZ_CODEC_* */
    int level;               /* 1-9 */
    int mode;                /* 0=compress, 1=decompress */
    size_t input_total;      /* bytes fed */
    size_t output_total;     /* bytes produced */
    int (*write_out)(const void *data, size_t len, void *user);
    void *user;              /* opaque user pointer for write_out */
};

/* --- One-shot API --- */

/* Compress data in one call. result must be freed with free().
 * Returns compressed size on success, negative on error. */
int mz_compress(const void *input, size_t input_len,
                void **result, size_t *result_len,
                int codec, int level);

/* Decompress data in one call. result must be freed with free().
 * Returns decompressed size on success, negative on error. */
int mz_decompress(const void *input, size_t input_len,
                  void **result, size_t *result_len,
                  int codec);

/* Estimate upper bound of compressed output for given input size. */
size_t mz_max_compressed_size(size_t input_len, int codec);

/* --- Streaming API --- */

/* Initialize a streaming compression/decompression session. */
int mz_stream_init(struct mz_stream *s, int codec, int level, int mode);

/* Feed data into the stream. May call write_out() multiple times. */
int mz_stream_feed(struct mz_stream *s, const void *data, size_t len);

/* Flush any buffered data. Call at end of stream. */
int mz_stream_flush(struct mz_stream *s);

/* Free resources held by the stream. */
void mz_stream_free(struct mz_stream *s);

/* --- Utility --- */
const char *mz_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_COMPRESS_H */
