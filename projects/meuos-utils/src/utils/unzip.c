/* unzip — PKZIP 解压工具
 *
 * 支持 PKZIP 格式（.zip）解压，包括 stored (0) 和 deflate (8) 压缩方法。
 * 复用 gzip.c 中已实现的 DEFLATE 解压逻辑。
 *
 * 用法：
 *   unzip [options] file.zip [files...]
 *   -d DIR    解压到指定目录
 *   -l        列出内容
 *   -t        测试完整性
 *   -o        覆盖已有文件
 *   -q        安静模式
 *   --help / --version
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* CRC32 (same as gzip) */
static uint32_t crc_table[256];
static int crc_table_init = 0;

static void init_crc32(void) {
    if (crc_table_init) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
        crc_table[i] = c;
    }
    crc_table_init = 1;
}

static uint32_t crc32_update(uint32_t crc, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    crc ^= 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++)
        crc = crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFU;
}

/* === DEFLATE decompression (same as gzip) === */
typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint32_t bitbuf;
    int bitcnt;
} br_t;

static void br_init(br_t *br, const uint8_t *data, size_t size) {
    br->data = data; br->size = size; br->pos = 0;
    br->bitbuf = 0; br->bitcnt = 0;
}

static int br_bit(br_t *br) {
    if (br->bitcnt == 0) {
        if (br->pos >= br->size) return -1;
        br->bitbuf = br->data[br->pos++];
        br->bitcnt = 8;
    }
    int bit = br->bitbuf & 1;
    br->bitbuf >>= 1;
    br->bitcnt--;
    return bit;
}

static int br_bits(br_t *br, int n) {
    int val = 0;
    for (int i = 0; i < n; i++) {
        int b = br_bit(br);
        if (b < 0) return -1;
        val |= (b << i);
    }
    return val;
}

static void br_align(br_t *br) { br->bitcnt = 0; br->bitbuf = 0; }

typedef struct { int counts[16]; int symbols[288]; } huff_t;

static void huff_build(huff_t *h, const int *lengths, int count) {
    memset(h->counts, 0, sizeof(h->counts));
    for (int i = 0; i < count; i++)
        if (lengths[i] > 0 && lengths[i] < 16)
            h->counts[lengths[i]]++;
    int offsets[16]; offsets[0] = 0;
    for (int i = 1; i < 16; i++)
        offsets[i] = offsets[i-1] + h->counts[i-1];
    for (int i = 0; i < count; i++)
        if (lengths[i] > 0 && lengths[i] < 16)
            h->symbols[offsets[lengths[i]]++] = i;
}

static int huff_decode(br_t *br, const huff_t *h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        int bit = br_bit(br);
        if (bit < 0) return -1;
        code |= bit;
        int count = h->counts[len];
        if (code - count < first)
            return h->symbols[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static huff_t fixed_lit, fixed_dist;
static int fixed_init = 0;

static void init_fixed(void) {
    if (fixed_init) return;
    int ll[288];
    for (int i = 0; i <= 143; i++) ll[i] = 8;
    for (int i = 144; i <= 255; i++) ll[i] = 9;
    for (int i = 256; i <= 279; i++) ll[i] = 7;
    for (int i = 280; i <= 287; i++) ll[i] = 8;
    huff_build(&fixed_lit, ll, 288);
    int dl[30]; for (int i = 0; i < 30; i++) dl[i] = 5;
    huff_build(&fixed_dist, dl, 30);
    fixed_init = 1;
}

static const int len_base[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const int len_extra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const int dist_base[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const int dist_extra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

static int inflate_block(br_t *br, uint8_t **out, size_t *out_len, size_t *out_cap) {
    int bfinal = br_bit(br);
    if (bfinal < 0) return -1;
    int btype = br_bits(br, 2);
    if (btype < 0) return -1;
    
    if (btype == 0) {
        br_align(br);
        if (br->pos + 4 > br->size) return -1;
        uint16_t len = br->data[br->pos] | (br->data[br->pos+1] << 8);
        br->pos += 4;
        if (br->pos + len > br->size) return -1;
        if (*out_len + len > *out_cap) {
            *out_cap = (*out_len + len) * 2;
            *out = realloc(*out, *out_cap);
            if (!*out) return -1;
        }
        memcpy(*out + *out_len, br->data + br->pos, len);
        *out_len += len;
        br->pos += len;
        return bfinal;
    }
    
    huff_t *lit_h, *dist_h;
    if (btype == 1) { init_fixed(); lit_h = &fixed_lit; dist_h = &fixed_dist; }
    else if (btype == 2) {
        int hlit = br_bits(br, 5) + 257;
        int hdist = br_bits(br, 5) + 1;
        int hclen = br_bits(br, 4) + 4;
        if (hlit < 0 || hdist < 0 || hclen < 0) return -1;
        static const int cl_order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
        int cl_lengths[19]; memset(cl_lengths, 0, sizeof(cl_lengths));
        for (int i = 0; i < hclen; i++) {
            cl_lengths[cl_order[i]] = br_bits(br, 3);
            if (cl_lengths[cl_order[i]] < 0) return -1;
        }
        huff_t cl_huff;
        huff_build(&cl_huff, cl_lengths, 19);
        int lengths[320]; int total = hlit + hdist; int idx = 0;
        while (idx < total) {
            int sym = huff_decode(br, &cl_huff);
            if (sym < 0) return -1;
            if (sym < 16) { lengths[idx++] = sym; }
            else if (sym == 16) {
                int rep = br_bits(br, 2) + 3;
                if (idx == 0) return -1;
                int prev = lengths[idx - 1];
                while (rep-- > 0 && idx < total) lengths[idx++] = prev;
            } else if (sym == 17) {
                int rep = br_bits(br, 3) + 3;
                while (rep-- > 0 && idx < total) lengths[idx++] = 0;
            } else if (sym == 18) {
                int rep = br_bits(br, 7) + 11;
                while (rep-- > 0 && idx < total) lengths[idx++] = 0;
            }
        }
        huff_build(lit_h = &fixed_lit, lengths, hlit);
        huff_build(dist_h = &fixed_dist, lengths + hlit, hdist);
    } else return -1;
    
    while (1) {
        int sym = huff_decode(br, lit_h);
        if (sym < 0) return -1;
        if (sym == 256) break;
        if (sym < 256) {
            if (*out_len >= *out_cap) {
                *out_cap = (*out_cap ? *out_cap * 2 : 1024);
                *out = realloc(*out, *out_cap);
                if (!*out) return -1;
            }
            (*out)[(*out_len)++] = (uint8_t)sym;
        } else {
            int li = sym - 257;
            if (li >= 29) return -1;
            int length = len_base[li];
            if (len_extra[li] > 0) {
                int extra = br_bits(br, len_extra[li]);
                if (extra < 0) return -1;
                length += extra;
            }
            int dsym = huff_decode(br, dist_h);
            if (dsym < 0 || dsym >= 30) return -1;
            int dist = dist_base[dsym];
            if (dist_extra[dsym] > 0) {
                int extra = br_bits(br, dist_extra[dsym]);
                if (extra < 0) return -1;
                dist += extra;
            }
            if (dist > (int)*out_len) return -1;
            if (*out_len + length > *out_cap) {
                while (*out_len + length > *out_cap)
                    *out_cap = (*out_cap ? *out_cap * 2 : 1024);
                *out = realloc(*out, *out_cap);
                if (!*out) return -1;
            }
            uint8_t *src = *out + *out_len - dist;
            for (int i = 0; i < length; i++)
                (*out)[*out_len + i] = src[i];
            *out_len += length;
        }
    }
    return bfinal;
}

static int inflate(const uint8_t *in, size_t in_size, uint8_t **out, size_t *out_len) {
    br_t br;
    br_init(&br, in, in_size);
    *out = NULL; *out_len = 0;
    size_t out_cap = 0;
    int bfinal = 0;
    while (!bfinal) {
        bfinal = inflate_block(&br, out, out_len, &out_cap);
        if (bfinal < 0) { free(*out); *out = NULL; return -1; }
    }
    return 0;
}

/* === PKZIP format === */
#pragma pack(push, 1)
typedef struct {
    uint32_t signature;
    uint16_t version_needed;
    uint16_t flags;
    uint16_t compression;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t name_len;
    uint16_t extra_len;
} zip_local_header_t;
#pragma pack(pop)

#define ZIP_SIG_LOCAL 0x04034b50
#define ZIP_SIG_CENTRAL 0x02014b50
#define ZIP_SIG_END 0x06054b50
#define ZIP_METHOD_STORED 0
#define ZIP_METHOD_DEFLATE 8

static const char *prog = "unzip";

typedef struct {
    char *name;
    uint16_t method;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t crc32;
    size_t data_offset;
} zip_entry_t;

/* Read entire zip file into memory */
static int read_zip_file(const char *path, uint8_t **data, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "%s: %s: %s\n", prog, path, strerror(errno));
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    *data = malloc(sz);
    *size = fread(*data, 1, sz, f);
    fclose(f);
    return 0;
}

/* Parse central directory entries from zip file */
static zip_entry_t *parse_zip_entries(const uint8_t *data, size_t size, int *count) {
    /* Find end of central directory record */
    int eocd_pos = -1;
    for (int i = (int)size - 22; i >= 0; i--) {
        if (data[i] == 0x50 && data[i+1] == 0x4b &&
            data[i+2] == 0x05 && data[i+3] == 0x06) {
            eocd_pos = i;
            break;
        }
    }
    if (eocd_pos < 0) {
        fprintf(stderr, "%s: not a valid zip file (no end of central directory)\n", prog);
        return NULL;
    }
    
    uint16_t num_entries = data[eocd_pos + 10] | (data[eocd_pos + 11] << 8);
    uint32_t cd_offset = data[eocd_pos + 16] | (data[eocd_pos + 17] << 8) |
                         (data[eocd_pos + 18] << 16) | (data[eocd_pos + 19] << 24);
    
    zip_entry_t *entries = calloc(num_entries, sizeof(zip_entry_t));
    *count = num_entries;
    
    size_t pos = cd_offset;
    for (int i = 0; i < num_entries; i++) {
        if (pos + 46 > size) break;
        uint32_t sig = data[pos] | (data[pos+1] << 8) |
                       (data[pos+2] << 16) | (data[pos+3] << 24);
        if (sig != ZIP_SIG_CENTRAL) break;
        
        uint16_t method = data[pos + 10] | (data[pos + 11] << 8);
        uint32_t crc = data[pos+16] | (data[pos+17] << 8) |
                       (data[pos+18] << 16) | (data[pos+19] << 24);
        uint32_t csize = data[pos+20] | (data[pos+21] << 8) |
                         (data[pos+22] << 16) | (data[pos+23] << 24);
        uint32_t usize = data[pos+24] | (data[pos+25] << 8) |
                         (data[pos+26] << 16) | (data[pos+27] << 24);
        uint16_t name_len = data[pos+28] | (data[pos+29] << 8);
        uint16_t extra_len = data[pos+30] | (data[pos+31] << 8);
        uint16_t comment_len = data[pos+32] | (data[pos+33] << 8);
        uint32_t local_offset = data[pos+42] | (data[pos+43] << 8) |
                                (data[pos+44] << 16) | (data[pos+45] << 24);
        
        entries[i].name = malloc(name_len + 1);
        memcpy(entries[i].name, data + pos + 46, name_len);
        entries[i].name[name_len] = '\0';
        entries[i].method = method;
        entries[i].compressed_size = csize;
        entries[i].uncompressed_size = usize;
        entries[i].crc32 = crc;
        
        /* Find local header to get data offset */
        if (local_offset + 30 <= size) {
            uint16_t local_name_len = data[local_offset + 26] | (data[local_offset + 27] << 8);
            uint16_t local_extra_len = data[local_offset + 28] | (data[local_offset + 29] << 8);
            entries[i].data_offset = local_offset + 30 + local_name_len + local_extra_len;
        }
        
        pos += 46 + name_len + extra_len + comment_len;
    }
    
    return entries;
}

/* Extract a single entry */
static int extract_entry(const uint8_t *zip_data, size_t zip_size,
                         const zip_entry_t *entry, const char *dest_dir, int overwrite) {
    /* Construct output path */
    char outpath[4096];
    if (dest_dir)
        snprintf(outpath, sizeof(outpath), "%s/%s", dest_dir, entry->name);
    else
        snprintf(outpath, sizeof(outpath), "%s", entry->name);
    
    /* Check if it's a directory entry (name ends with /) */
    size_t namelen = strlen(entry->name);
    if (namelen > 0 && entry->name[namelen - 1] == '/') {
        /* Create directory */
        /* mkdir -p style */
        char *path_copy = strdup(outpath);
        for (char *p = path_copy + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                mkdir(path_copy, 0755);
                *p = '/';
            }
        }
        mkdir(path_copy, 0755);
        free(path_copy);
        return 0;
    }
    
    /* Check overwrite */
    if (!overwrite && access(outpath, F_OK) == 0) {
        fprintf(stderr, "%s: %s already exists\n", prog, outpath);
        return 1;
    }
    
    /* Create parent directories */
    char *path_copy = strdup(outpath);
    char *last_slash = strrchr(path_copy, '/');
    if (last_slash) {
        *last_slash = '\0';
        char *p = path_copy + 1;
        while (*p) {
            if (*p == '/') {
                *p = '\0';
                mkdir(path_copy, 0755);
                *p = '/';
            }
            p++;
        }
        mkdir(path_copy, 0755);
    }
    free(path_copy);
    
    /* Get compressed data */
    if (entry->data_offset + entry->compressed_size > zip_size) {
        fprintf(stderr, "%s: %s: truncated data\n", prog, entry->name);
        return 1;
    }
    
    const uint8_t *comp_data = zip_data + entry->data_offset;
    uint8_t *out_data = NULL;
    size_t out_len = 0;
    
    if (entry->method == ZIP_METHOD_STORED) {
        /* No compression — data is stored as-is */
        out_data = malloc(entry->uncompressed_size);
        memcpy(out_data, comp_data, entry->uncompressed_size);
        out_len = entry->uncompressed_size;
    } else if (entry->method == ZIP_METHOD_DEFLATE) {
        /* DEFLATE compressed */
        if (inflate(comp_data, entry->compressed_size, &out_data, &out_len) != 0) {
            fprintf(stderr, "%s: %s: decompression error\n", prog, entry->name);
            return 1;
        }
    } else {
        fprintf(stderr, "%s: %s: unsupported compression method %d\n", prog, entry->name, entry->method);
        return 1;
    }
    
    /* Verify CRC32 */
    init_crc32();
    uint32_t crc = crc32_update(0, out_data, out_len);
    if (crc != entry->crc32) {
        fprintf(stderr, "%s: %s: CRC mismatch\n", prog, entry->name);
    }
    
    /* Write output file */
    FILE *f = fopen(outpath, "wb");
    if (!f) {
        fprintf(stderr, "%s: %s: %s\n", prog, outpath, strerror(errno));
        free(out_data);
        return 1;
    }
    fwrite(out_data, 1, out_len, f);
    fclose(f);
    free(out_data);
    
    return 0;
}

/* List entries */
static int list_entries(const zip_entry_t *entries, int count) {
    printf("  Length    Date    Time   Name\n");
    printf("---------  ---------- -----   ----\n");
    int total_uncompressed = 0;
    int total_compressed = 0;
    for (int i = 0; i < count; i++) {
        printf("%9u  %s   %s\n", entries[i].uncompressed_size,
               "----/--/--", entries[i].name);
        total_uncompressed += entries[i].uncompressed_size;
        total_compressed += entries[i].compressed_size;
    }
    printf("---------                     -------\n");
    printf("%9d                     %d files\n", total_uncompressed, count);
    return 0;
}

/* Test entries */
static int test_entries(const uint8_t *zip_data, size_t zip_size,
                        const zip_entry_t *entries, int count) {
    int errors = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].method != ZIP_METHOD_STORED &&
            entries[i].method != ZIP_METHOD_DEFLATE) {
            fprintf(stderr, "%s: %s: unsupported method\n", prog, entries[i].name);
            errors++;
            continue;
        }
        
        if (entries[i].data_offset + entries[i].compressed_size > zip_size) {
            fprintf(stderr, "%s: %s: truncated\n", prog, entries[i].name);
            errors++;
            continue;
        }
        
        uint8_t *out_data = NULL;
        size_t out_len = 0;
        const uint8_t *comp_data = zip_data + entries[i].data_offset;
        
        if (entries[i].method == ZIP_METHOD_STORED) {
            out_data = malloc(entries[i].uncompressed_size);
            memcpy(out_data, comp_data, entries[i].uncompressed_size);
            out_len = entries[i].uncompressed_size;
        } else {
            if (inflate(comp_data, entries[i].compressed_size, &out_data, &out_len) != 0) {
                fprintf(stderr, "%s: %s: decompression error\n", prog, entries[i].name);
                errors++;
                free(out_data);
                continue;
            }
        }
        
        init_crc32();
        uint32_t crc = crc32_update(0, out_data, out_len);
        free(out_data);
        
        if (crc != entries[i].crc32) {
            fprintf(stderr, "%s: %s: CRC mismatch\n", prog, entries[i].name);
            errors++;
        }
    }
    
    if (errors == 0)
        printf("No errors detected in %d entries\n", count);
    else
        printf("%d error(s) detected\n", errors);
    
    return errors > 0 ? 1 : 0;
}

static void usage(void) {
    printf(
        "unzip — PKZIP archive extractor (meuos-utils)\n"
        "\n"
        "usage: unzip [options] file.zip [files...]\n"
        "\n"
        "options:\n"
        "  -d DIR    extract to directory\n"
        "  -l        list contents\n"
        "  -t        test integrity\n"
        "  -o        overwrite existing files\n"
        "  -q        quiet mode\n"
        "  --help    show help\n"
        "  --version show version\n");
}

int main(int argc, char **argv) {
    int mode_list = 0;
    int mode_test = 0;
    int overwrite = 0;
    int quiet = 0;
    const char *dest_dir = NULL;
    int oi = 1;
    
    while (oi < argc) {
        const char *opt = argv[oi];
        if (strcmp(opt, "--help") == 0) { usage(); return 0; }
        if (strcmp(opt, "--version") == 0) { printf("unzip (meuos-utils)\n"); return 0; }
        if (strcmp(opt, "-l") == 0) { mode_list = 1; oi++; continue; }
        if (strcmp(opt, "-t") == 0) { mode_test = 1; oi++; continue; }
        if (strcmp(opt, "-o") == 0) { overwrite = 1; oi++; continue; }
        if (strcmp(opt, "-q") == 0) { quiet = 1; oi++; continue; }
        if (strcmp(opt, "-d") == 0) {
            if (++oi >= argc) { fprintf(stderr, "%s: -d requires argument\n", prog); return 2; }
            dest_dir = argv[oi]; oi++; continue;
        }
        if (opt[0] == '-' && opt[1] != '\0') {
            fprintf(stderr, "%s: unknown option %s\n", prog, opt);
            return 2;
        }
        break;
    }
    
    if (oi >= argc) {
        fprintf(stderr, "%s: missing zip file\n", prog);
        usage();
        return 2;
    }
    
    const char *zip_path = argv[oi++];
    
    uint8_t *zip_data = NULL;
    size_t zip_size = 0;
    if (read_zip_file(zip_path, &zip_data, &zip_size) != 0)
        return 1;
    
    int entry_count = 0;
    zip_entry_t *entries = parse_zip_entries(zip_data, zip_size, &entry_count);
    if (!entries || entry_count == 0) {
        fprintf(stderr, "%s: no entries found in %s\n", prog, zip_path);
        free(zip_data);
        return 1;
    }
    
    /* Filter: if specific files are listed, only extract those */
    const char **want = NULL;
    int want_count = 0;
    if (oi < argc && !mode_list && !mode_test) {
        want = (const char **)(argv + oi);
        want_count = argc - oi;
    }
    
    int ret = 0;
    
    if (mode_list) {
        list_entries(entries, entry_count);
    } else if (mode_test) {
        ret = test_entries(zip_data, zip_size, entries, entry_count);
    } else {
        /* Extract */
        if (dest_dir) {
            /* Create dest dir if not exists */
            mkdir(dest_dir, 0755);
        }
        
        for (int i = 0; i < entry_count; i++) {
            /* Check if this entry is wanted */
            if (want_count > 0) {
                int found = 0;
                for (int j = 0; j < want_count; j++) {
                    if (strcmp(entries[i].name, want[j]) == 0) {
                        found = 1; break;
                    }
                }
                if (!found) continue;
            }
            
            if (!quiet)
                printf("  inflating: %s\n", entries[i].name);
            
            if (extract_entry(zip_data, zip_size, &entries[i], dest_dir, overwrite) != 0)
                ret = 1;
        }
    }
    
    /* Cleanup */
    for (int i = 0; i < entry_count; i++) free(entries[i].name);
    free(entries);
    free(zip_data);
    
    return ret;
}
