/* mz_zip.c — PKZIP archive format reader
 *
 * PKZIP format (APPNOTE 6.3.0):
 *   - Local file headers + data, scattered throughout file
 *   - Central directory at end of file (pointed to by EOCD record)
 *   - End of central directory (EOCD) record at very end
 *
 * This implementation provides read-only access:
 *   - Parse EOCD to find central directory
 *   - Parse central directory entries
 *   - Extract files (stored or DEFLATE compressed)
 *
 * API:
 *   struct mz_zip_entry — entry metadata
 *   struct mz_zip_reader — opaque reader context
 *   mz_zip_reader_open() / mz_zip_reader_close()
 *   mz_zip_reader_count() / mz_zip_reader_entry()
 *   mz_zip_reader_extract() / mz_zip_reader_find()
 */
#include "mz.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MZ_ZIP_EOCD_SIG       0x06054b50
#define MZ_ZIP_CDIR_SIG       0x02014b50
#define MZ_ZIP_LOCAL_SIG      0x04034b50
#define MZ_ZIP_STORED         0
#define MZ_ZIP_DEFLATED       8

struct mz_zip_reader {
    const uint8_t *data;
    size_t data_len;
    struct mz_zip_entry *entries;
    int count;
};

/* Read little-endian values from buffer */
static uint16_t
rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t
rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int
mz_zip_reader_open(struct mz_zip_reader **reader, const void *data, size_t len)
{
    if (!reader || !data) return MZ_ERR_PARAM;
    if (len < 22) return MZ_ERR_DATA;  /* minimum EOCD size */

    const uint8_t *d = data;

    /* Find EOCD record by scanning backwards */
    size_t eocd_pos = (size_t)-1;
    size_t scan_start = (len >= 65557) ? len - 65557 : 0;
    for (size_t i = len - 22 + 1; i-- > scan_start; ) {
        if (d[i] == 0x50 && d[i + 1] == 0x4b && d[i + 2] == 0x05 &&
            d[i + 3] == 0x06) {
            eocd_pos = i;
            break;
        }
    }
    if (eocd_pos == (size_t)-1) return MZ_ERR_DATA;

    /* Parse EOCD */
    uint16_t total_entries = rd16(d + eocd_pos + 10);
    uint32_t cdir_offset = rd32(d + eocd_pos + 16);
    uint32_t cdir_size = rd32(d + eocd_pos + 12);

    if (cdir_offset + cdir_size > len) return MZ_ERR_DATA;
    if (total_entries == 0) {
        *reader = calloc(1, sizeof(struct mz_zip_reader));
        if (!*reader) return MZ_ERR_MEMORY;
        (*reader)->data = data;
        (*reader)->data_len = len;
        (*reader)->count = 0;
        return MZ_OK;
    }

    struct mz_zip_reader *r = calloc(1, sizeof(*r));
    if (!r) return MZ_ERR_MEMORY;
    r->data = d;
    r->data_len = len;
    r->entries = calloc(total_entries, sizeof(struct mz_zip_entry));
    if (!r->entries) { free(r); return MZ_ERR_MEMORY; }

    /* Parse central directory */
    size_t pos = cdir_offset;
    for (int i = 0; i < total_entries; i++) {
        if (pos + 46 > len) goto fail;
        if (rd32(d + pos) != MZ_ZIP_CDIR_SIG) goto fail;

        struct mz_zip_entry *e = &r->entries[i];
        e->method = rd16(d + pos + 10);
        e->crc32 = rd32(d + pos + 16);
        e->compressed_size = rd32(d + pos + 20);
        e->uncompressed_size = rd32(d + pos + 24);
        uint16_t name_len = rd16(d + pos + 28);
        uint16_t extra_len = rd16(d + pos + 30);
        uint16_t comment_len = rd16(d + pos + 32);
        e->local_header_offset = rd32(d + pos + 42);

        if (pos + 46 + name_len > len) goto fail;
        size_t copy_len = name_len < sizeof(e->name) - 1 ? name_len : sizeof(e->name) - 1;
        memcpy(e->name, d + pos + 46, copy_len);
        e->name[copy_len] = '\0';

        pos += 46 + name_len + extra_len + comment_len;
        r->count++;
    }

    *reader = r;
    return MZ_OK;

fail:
    free(r->entries);
    free(r);
    return MZ_ERR_DATA;
}

void
mz_zip_reader_close(struct mz_zip_reader *reader)
{
    if (!reader) return;
    free(reader->entries);
    free(reader);
}

int
mz_zip_reader_count(struct mz_zip_reader *reader)
{
    return reader ? reader->count : 0;
}

const struct mz_zip_entry *
mz_zip_reader_entry(struct mz_zip_reader *reader, int idx)
{
    if (!reader || idx < 0 || idx >= reader->count) return NULL;
    return &reader->entries[idx];
}

int
mz_zip_reader_find(struct mz_zip_reader *reader, const char *name)
{
    if (!reader || !name) return -1;
    for (int i = 0; i < reader->count; i++) {
        if (strcmp(reader->entries[i].name, name) == 0)
            return i;
    }
    return -1;
}

int
mz_zip_reader_extract(struct mz_zip_reader *reader, int idx,
                      void **out_data, size_t *out_len)
{
    if (!reader || !out_data || !out_len) return MZ_ERR_PARAM;
    if (idx < 0 || idx >= reader->count) return MZ_ERR_PARAM;

    const struct mz_zip_entry *e = &reader->entries[idx];
    const uint8_t *d = reader->data;
    size_t len = reader->data_len;

    /* Parse local file header to find data offset */
    if (e->local_header_offset + 30 > len) return MZ_ERR_DATA;
    if (rd32(d + e->local_header_offset) != MZ_ZIP_LOCAL_SIG) return MZ_ERR_DATA;

    uint16_t name_len = rd16(d + e->local_header_offset + 26);
    uint16_t extra_len = rd16(d + e->local_header_offset + 28);
    size_t data_offset = e->local_header_offset + 30 + name_len + extra_len;

    if (data_offset + e->compressed_size > len) return MZ_ERR_DATA;

    if (e->method == MZ_ZIP_STORED) {
        /* Stored: no compression */
        void *out = malloc(e->uncompressed_size > 0 ? e->uncompressed_size : 1);
        if (!out) return MZ_ERR_MEMORY;
        memcpy(out, d + data_offset, e->uncompressed_size);
        *out_data = out;
        *out_len = e->uncompressed_size;

        /* Verify CRC */
        if (mz_crc32(out, e->uncompressed_size) != e->crc32) {
            free(out);
            return MZ_ERR_DATA;
        }
        return (int)e->uncompressed_size;
    } else if (e->method == MZ_ZIP_DEFLATED) {
        /* DEFLATE compressed */
        void *raw = NULL;
        size_t raw_len = 0;
        int rc = mz_deflate_decompress(d + data_offset, e->compressed_size,
                                        &raw, &raw_len);
        if (rc <= 0) return rc;

        /* Verify size */
        if (raw_len != e->uncompressed_size) {
            free(raw);
            return MZ_ERR_DATA;
        }
        /* Verify CRC */
        if (mz_crc32(raw, raw_len) != e->crc32) {
            free(raw);
            return MZ_ERR_DATA;
        }

        *out_data = raw;
        *out_len = raw_len;
        return (int)raw_len;
    }

    return MZ_ERR_DATA;  /* unsupported method */
}
