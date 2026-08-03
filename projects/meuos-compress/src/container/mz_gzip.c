/* mz_gzip.c — RFC 1952 Gzip container format
 *
 * Gzip format:
 *   Header (10+ bytes):
 *     ID1 (0x1f), ID2 (0x8b), CM (8=deflate), FLG, MTIME (4), XFL, OS
 *     [optional extra fields based on FLG]
 *   Body: DEFLATE compressed data
 *   Footer (8 bytes):
 *     CRC32 (4 bytes LE), ISIZE (4 bytes LE)
 *
 * API:
 *   mz_gzip_compress() — wrap data with gzip header + DEFLATE(stored) + footer
 *   mz_gzip_decompress() — parse header, decompress DEFLATE, verify CRC32
 */
#include "mz.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MZ_GZIP_MAGIC1 0x1f
#define MZ_GZIP_MAGIC2 0x8b
#define MZ_GZIP_CM_DEFLATE 8

/* FLG bits */
#define MZ_GZIP_FTEXT    0x01
#define MZ_GZIP_FHCRC    0x02
#define MZ_GZIP_FEXTRA   0x04
#define MZ_GZIP_FNAME    0x08
#define MZ_GZIP_FCOMMENT 0x10

int
mz_gzip_compress(const void *input, size_t inlen, void **result, size_t *result_len)
{
    if (!input || !result || !result_len) return MZ_ERR_PARAM;

    /* DEFLATE compress first */
    void *deflate_out = NULL;
    size_t deflate_len = 0;
    int rc = mz_deflate_compress(input, inlen, &deflate_out, &deflate_len);
    if (rc <= 0) return rc;

    /* Gzip header (10 bytes) + DEFLATE body + CRC32 (4) + ISIZE (4) */
    size_t total = 10 + deflate_len + 8;
    uint8_t *out = malloc(total);
    if (!out) { free(deflate_out); return MZ_ERR_MEMORY; }

    size_t op = 0;
    /* Header */
    out[op++] = MZ_GZIP_MAGIC1;
    out[op++] = MZ_GZIP_MAGIC2;
    out[op++] = MZ_GZIP_CM_DEFLATE;
    out[op++] = 0;  /* FLG: no extras */
    out[op++] = 0; out[op++] = 0; out[op++] = 0; out[op++] = 0;  /* MTIME */
    out[op++] = 0;  /* XFL */
    out[op++] = 3;  /* OS: Unix */

    /* DEFLATE body */
    memcpy(out + op, deflate_out, deflate_len);
    op += deflate_len;
    free(deflate_out);

    /* CRC32 */
    uint32_t crc = mz_crc32(input, inlen);
    out[op++] = crc & 0xFF;
    out[op++] = (crc >> 8) & 0xFF;
    out[op++] = (crc >> 16) & 0xFF;
    out[op++] = (crc >> 24) & 0xFF;

    /* ISIZE (original size mod 2^32) */
    uint32_t isize = (uint32_t)inlen;
    out[op++] = isize & 0xFF;
    out[op++] = (isize >> 8) & 0xFF;
    out[op++] = (isize >> 16) & 0xFF;
    out[op++] = (isize >> 24) & 0xFF;

    *result = out;
    *result_len = op;
    return (int)op;
}

int
mz_gzip_decompress(const void *input, size_t inlen, void **result, size_t *result_len)
{
    if (!input || !result || !result_len) return MZ_ERR_PARAM;
    if (inlen < 18) return MZ_ERR_DATA;  /* 10 header + 8 footer minimum */

    const uint8_t *in = input;

    /* Verify magic */
    if (in[0] != MZ_GZIP_MAGIC1 || in[1] != MZ_GZIP_MAGIC2) return MZ_ERR_DATA;
    if (in[2] != MZ_GZIP_CM_DEFLATE) return MZ_ERR_DATA;

    uint8_t flg = in[3];
    size_t pos = 10;  /* skip fixed header */

    /* Skip optional fields */
    if (flg & MZ_GZIP_FEXTRA) {
        if (pos + 2 > inlen) return MZ_ERR_DATA;
        size_t xlen = in[pos] | (in[pos + 1] << 8);
        pos += 2 + xlen;
        if (pos > inlen) return MZ_ERR_DATA;
    }
    if (flg & MZ_GZIP_FNAME) {
        while (pos < inlen && in[pos] != 0) pos++;
        pos++;  /* skip null terminator */
        if (pos > inlen) return MZ_ERR_DATA;
    }
    if (flg & MZ_GZIP_FCOMMENT) {
        while (pos < inlen && in[pos] != 0) pos++;
        pos++;
        if (pos > inlen) return MZ_ERR_DATA;
    }
    if (flg & MZ_GZIP_FHCRC) {
        pos += 2;
        if (pos > inlen) return MZ_ERR_DATA;
    }

    /* Footer is last 8 bytes */
    if (inlen < pos + 8) return MZ_ERR_DATA;
    size_t deflate_end = inlen - 8;
    if (deflate_end < pos) return MZ_ERR_DATA;

    /* Read expected CRC and size from footer */
    uint32_t expected_crc = (uint32_t)in[deflate_end]
        | ((uint32_t)in[deflate_end + 1] << 8)
        | ((uint32_t)in[deflate_end + 2] << 16)
        | ((uint32_t)in[deflate_end + 3] << 24);
    uint32_t expected_size = (uint32_t)in[deflate_end + 4]
        | ((uint32_t)in[deflate_end + 5] << 8)
        | ((uint32_t)in[deflate_end + 6] << 16)
        | ((uint32_t)in[deflate_end + 7] << 24);

    /* DEFLATE decompress */
    void *raw_out = NULL;
    size_t raw_len = 0;
    int rc = mz_deflate_decompress(in + pos, deflate_end - pos, &raw_out, &raw_len);
    if (rc <= 0) return rc;

    /* Verify CRC */
    uint32_t actual_crc = mz_crc32(raw_out, raw_len);
    if (actual_crc != expected_crc) {
        free(raw_out);
        return MZ_ERR_DATA;
    }

    /* Verify size (mod 2^32) */
    if ((uint32_t)raw_len != expected_size) {
        free(raw_out);
        return MZ_ERR_DATA;
    }

    *result = raw_out;
    *result_len = raw_len;
    return (int)raw_len;
}
