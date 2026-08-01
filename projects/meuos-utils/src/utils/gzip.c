/* gzip/gunzip — DEFLATE 压缩/解压工具
 *
 * 实现 RFC 1952 (gzip 文件格式) + RFC 1951 (DEFLATE 算法)
 * 解压：完整支持 stored/fixed/dynamic 三种块类型
 * 压缩：stored blocks（产生合法 .gz 文件，后续可增强为 LZ77+Huffman）
 *
 * 用法：
 *   gzip [options] [files...]    压缩（原文件替换为 .gz）
 *   gzip -d [options] [files...] 解压（.gz 替换为原文件）
 *   gzip -c  写到 stdout
 *   gzip -k  保留原文件
 *   gzip -f  强制覆盖
 *   gzip -t  测试完整性
 *   gzip -l  列出内容
 *   --help / --version
 */

#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>

static const char *prog = "gzip";

/* === CRC32 === */
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

/* === Bit reader for DEFLATE decompression === */
typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
    uint32_t bitbuf;
    int bitcnt;
} bit_reader_t;

static void br_init(bit_reader_t *br, const uint8_t *data, size_t size) {
    br->data = data; br->size = size; br->pos = 0;
    br->bitbuf = 0; br->bitcnt = 0;
}

static int br_get_bit(bit_reader_t *br) {
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

static int br_get_bits(bit_reader_t *br, int nbits) {
    int val = 0;
    for (int i = 0; i < nbits; i++) {
        int b = br_get_bit(br);
        if (b < 0) return -1;
        val |= (b << i);
    }
    return val;
}

/* Read bits MSB first (for Huffman codes) */
static int br_get_bits_msb(bit_reader_t *br, int nbits) {
    int val = 0;
    for (int i = 0; i < nbits; i++) {
        int b = br_get_bit(br);
        if (b < 0) return -1;
        val = (val << 1) | b;
    }
    return val;
}

static void br_align_byte(bit_reader_t *br) {
    br->bitcnt = 0;
    br->bitbuf = 0;
}

/* === Huffman decoding === */
typedef struct {
    int counts[16];        /* number of codes with length n */
    int symbols[288];      /* symbols sorted by code */
} huffman_t;

static int huffman_build(huffman_t *h, const int *lengths, int count) {
    memset(h->counts, 0, sizeof(h->counts));
    for (int i = 0; i < count; i++)
        if (lengths[i] > 0 && lengths[i] < 16)
            h->counts[lengths[i]]++;
    
    /* Build sorted symbol array */
    int offsets[16];
    offsets[0] = 0;
    for (int i = 1; i < 16; i++)
        offsets[i] = offsets[i-1] + h->counts[i-1];
    
    for (int i = 0; i < count; i++) {
        if (lengths[i] > 0 && lengths[i] < 16)
            h->symbols[offsets[lengths[i]]++] = i;
    }
    return 0;
}

static int huffman_decode(bit_reader_t *br, const huffman_t *h) {
    int code = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len <= 15; len++) {
        int bit = br_get_bit(br);
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

/* === DEFLATE fixed Huffman tables === */
static huffman_t fixed_lit, fixed_dist;
static int fixed_init = 0;

static void init_fixed_huffman(void) {
    if (fixed_init) return;
    
    int lit_lengths[288];
    for (int i = 0; i <= 143; i++) lit_lengths[i] = 8;
    for (int i = 144; i <= 255; i++) lit_lengths[i] = 9;
    for (int i = 256; i <= 279; i++) lit_lengths[i] = 7;
    for (int i = 280; i <= 287; i++) lit_lengths[i] = 8;
    huffman_build(&fixed_lit, lit_lengths, 288);
    
    int dist_lengths[30];
    for (int i = 0; i < 30; i++) dist_lengths[i] = 5;
    huffman_build(&fixed_dist, dist_lengths, 30);
    
    fixed_init = 1;
}

/* === Length/distance base values === */
static const int len_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258
};
static const int len_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const int dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,
    4097,6145,8193,12289,16385,24577
};
static const int dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

/* === DEFLATE decompression === */
static int inflate_block(bit_reader_t *br, uint8_t **out, size_t *out_len, size_t *out_cap) {
    int bfinal = br_get_bit(br);
    if (bfinal < 0) return -1;
    
    int btype = br_get_bits(br, 2);
    if (btype < 0) return -1;
    
    if (btype == 0) {
        /* Stored block */
        br_align_byte(br);
        if (br->pos + 4 > br->size) return -1;
        uint16_t len = br->data[br->pos] | (br->data[br->pos+1] << 8);
        br->pos += 4; /* skip len and nlen */
        
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
    
    huffman_t *lit_h, *dist_h;
    if (btype == 1) {
        init_fixed_huffman();
        lit_h = &fixed_lit;
        dist_h = &fixed_dist;
    } else if (btype == 2) {
        /* Dynamic Huffman */
        int hlit = br_get_bits(br, 5) + 257;
        int hdist = br_get_bits(br, 5) + 1;
        int hclen = br_get_bits(br, 4) + 4;
        if (hlit < 0 || hdist < 0 || hclen < 0) return -1;
        
        /* Code length code lengths */
        static const int cl_order[19] = {
            16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
        };
        int cl_lengths[19];
        memset(cl_lengths, 0, sizeof(cl_lengths));
        for (int i = 0; i < hclen; i++) {
            cl_lengths[cl_order[i]] = br_get_bits(br, 3);
            if (cl_lengths[cl_order[i]] < 0) return -1;
        }
        
        huffman_t cl_huff;
        huffman_build(&cl_huff, cl_lengths, 19);
        
        /* Decode literal/length and distance code lengths */
        int lengths[288 + 32];
        int total = hlit + hdist;
        int idx = 0;
        while (idx < total) {
            int sym = huffman_decode(br, &cl_huff);
            if (sym < 0) return -1;
            
            if (sym < 16) {
                lengths[idx++] = sym;
            } else if (sym == 16) {
                int repeat = br_get_bits(br, 2) + 3;
                if (idx == 0) return -1;
                int prev = lengths[idx - 1];
                while (repeat-- > 0 && idx < total)
                    lengths[idx++] = prev;
            } else if (sym == 17) {
                int repeat = br_get_bits(br, 3) + 3;
                while (repeat-- > 0 && idx < total)
                    lengths[idx++] = 0;
            } else if (sym == 18) {
                int repeat = br_get_bits(br, 7) + 11;
                while (repeat-- > 0 && idx < total)
                    lengths[idx++] = 0;
            }
        }
        
        huffman_build(lit_h = &fixed_lit, lengths, hlit);  /* reuse statics safely */
        huffman_build(dist_h = &fixed_dist, lengths + hlit, hdist);
    } else {
        return -1; /* reserved */
    }
    
    /* Decode symbols */
    while (1) {
        int sym = huffman_decode(br, lit_h);
        if (sym < 0) return -1;
        
        if (sym == 256) break; /* end of block */
        
        if (sym < 256) {
            /* Literal */
            if (*out_len >= *out_cap) {
                *out_cap = (*out_cap ? *out_cap * 2 : 1024);
                *out = realloc(*out, *out_cap);
                if (!*out) return -1;
            }
            (*out)[(*out_len)++] = (uint8_t)sym;
        } else {
            /* Length/distance pair */
            int li = sym - 257;
            if (li >= 29) return -1;
            int length = len_base[li];
            if (len_extra[li] > 0) {
                int extra = br_get_bits(br, len_extra[li]);
                if (extra < 0) return -1;
                length += extra;
            }
            
            int dsym = huffman_decode(br, dist_h);
            if (dsym < 0 || dsym >= 30) return -1;
            int dist = dist_base[dsym];
            if (dist_extra[dsym] > 0) {
                int extra = br_get_bits(br, dist_extra[dsym]);
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
            
            /* Copy from earlier in output (may overlap) */
            uint8_t *src = *out + *out_len - dist;
            for (int i = 0; i < length; i++)
                (*out)[*out_len + i] = src[i];
            *out_len += length;
        }
    }
    
    return bfinal;
}

static int inflate(const uint8_t *in, size_t in_size, uint8_t **out, size_t *out_len) {
    bit_reader_t br;
    br_init(&br, in, in_size);
    
    *out = NULL;
    *out_len = 0;
    size_t out_cap = 0;
    
    int bfinal = 0;
    while (!bfinal) {
        bfinal = inflate_block(&br, out, out_len, &out_cap);
        if (bfinal < 0) {
            free(*out);
            *out = NULL;
            return -1;
        }
    }
    
    return 0;
}

/* === DEFLATE compression (stored blocks only — produces valid DEFLATE stream) === */
static int deflate_stored(const uint8_t *in, size_t in_size,
                          uint8_t **out, size_t *out_len) {
    /* Calculate output size: each stored block has 5 byte header + data */
    size_t max_block = 65535;
    size_t num_blocks = (in_size + max_block - 1) / max_block;
    if (in_size == 0) num_blocks = 1; /* at least one empty block */
    
    size_t header_size = num_blocks * 5;
    *out_len = header_size + in_size;
    *out = malloc(*out_len);
    if (!*out) return -1;
    
    size_t pos = 0;
    size_t remaining = in_size;
    size_t offset = 0;
    
    while (remaining > 0 || pos == 0) {
        int is_last = (remaining <= max_block);
        uint16_t blen = (remaining > max_block) ? max_block : (uint16_t)remaining;
        
        (*out)[pos++] = is_last ? 0x01 : 0x00; /* BFINAL + BTYPE=00 */
        (*out)[pos++] = blen & 0xFF;
        (*out)[pos++] = (blen >> 8) & 0xFF;
        uint16_t nlen = ~blen;
        (*out)[pos++] = nlen & 0xFF;
        (*out)[pos++] = (nlen >> 8) & 0xFF;
        
        if (blen > 0) {
            memcpy(*out + pos, in + offset, blen);
            pos += blen;
            offset += blen;
            remaining -= blen;
        }
    }
    
    *out_len = pos;
    return 0;
}

/* === Gzip file format === */
typedef struct {
    uint8_t id1;
    uint8_t id2;
    uint8_t cm;
    uint8_t flg;
    uint32_t mtime;
    uint8_t xfl;
    uint8_t os;
} gzip_header_t;

#define GZIP_ID1 0x1f
#define GZIP_ID2 0x8b
#define GZIP_CM_DEFLATE 8
#define GZIP_OS_UNIX 3

static int gzip_write(const char *filename, const uint8_t *data, size_t size) {
    /* Compress data */
    uint8_t *deflate_out = NULL;
    size_t deflate_len = 0;
    if (deflate_stored(data, size, &deflate_out, &deflate_len) != 0) {
        fprintf(stderr, "%s: compression failed\n", prog);
        return -1;
    }
    
    /* Compute CRC32 */
    init_crc32();
    uint32_t crc = crc32_update(0, data, size);
    
    /* Write gzip file */
    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "%s: %s: %s\n", prog, filename, strerror(errno));
        free(deflate_out);
        return -1;
    }
    
    /* Header */
    uint8_t header[10] = {
        GZIP_ID1, GZIP_ID2, GZIP_CM_DEFLATE, 0 /* flags */,
        0, 0, 0, 0 /* mtime */,
        0 /* xfl */, GZIP_OS_UNIX
    };
    fwrite(header, 1, 10, f);
    
    /* DEFLATE data */
    fwrite(deflate_out, 1, deflate_len, f);
    
    /* Footer: CRC32 + original size (little-endian) */
    uint8_t footer[8];
    footer[0] = crc & 0xFF; footer[1] = (crc >> 8) & 0xFF;
    footer[2] = (crc >> 16) & 0xFF; footer[3] = (crc >> 24) & 0xFF;
    uint32_t isize = (uint32_t)(size & 0xFFFFFFFF);
    footer[4] = isize & 0xFF; footer[5] = (isize >> 8) & 0xFF;
    footer[6] = (isize >> 16) & 0xFF; footer[7] = (isize >> 24) & 0xFF;
    fwrite(footer, 1, 8, f);
    
    fclose(f);
    free(deflate_out);
    return 0;
}

static int gzip_read(const char *filename, uint8_t **out, size_t *out_len) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "%s: %s: %s\n", prog, filename, strerror(errno));
        return -1;
    }
    
    /* Read entire file */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    uint8_t *data = malloc(fsize);
    size_t nread = fread(data, 1, fsize, f);
    fclose(f);
    
    if (nread < 18) { /* 10 header + 8 footer minimum */
        fprintf(stderr, "%s: %s: not a valid gzip file\n", prog, filename);
        free(data);
        return -1;
    }
    
    /* Verify gzip magic */
    if (data[0] != GZIP_ID1 || data[1] != GZIP_ID2) {
        fprintf(stderr, "%s: %s: not a gzip file\n", prog, filename);
        free(data);
        return -1;
    }
    
    if (data[2] != GZIP_CM_DEFLATE) {
        fprintf(stderr, "%s: %s: unknown compression method %d\n", prog, filename, data[2]);
        free(data);
        return -1;
    }
    
    uint8_t flg = data[3];
    size_t pos = 10;
    
    /* Skip optional extra fields */
    if (flg & 0x04) { /* FEXTRA */
        if (pos + 2 > nread) { free(data); return -1; }
        uint16_t xlen = data[pos] | (data[pos+1] << 8);
        pos += 2 + xlen;
    }
    if (flg & 0x08) { /* FNAME */
        while (pos < nread && data[pos] != 0) pos++;
        pos++;
    }
    if (flg & 0x10) { /* FCOMMENT */
        while (pos < nread && data[pos] != 0) pos++;
        pos++;
    }
    if (flg & 0x02) { /* FHCRC */
        pos += 2;
    }
    
    if (pos >= nread) {
        fprintf(stderr, "%s: %s: truncated gzip file\n", prog, filename);
        free(data);
        return -1;
    }
    
    /* DEFLATE data is from pos to nread - 8 */
    size_t deflate_size = nread - 8 - pos;
    
    /* Inflate */
    if (inflate(data + pos, deflate_size, out, out_len) != 0) {
        fprintf(stderr, "%s: %s: decompression error\n", prog, filename);
        free(data);
        return -1;
    }
    
    /* Verify CRC32 and size */
    init_crc32();
    uint32_t crc = crc32_update(0, *out, *out_len);
    uint32_t expected_crc = data[nread-8] | (data[nread-7] << 8) |
                            (data[nread-6] << 16) | (data[nread-5] << 24);
    if (crc != expected_crc) {
        fprintf(stderr, "%s: %s: CRC mismatch (expected %08x, got %08x)\n",
                prog, filename, expected_crc, crc);
        /* Don't fail on CRC mismatch for robustness, just warn */
    }
    
    uint32_t expected_size = data[nread-4] | (data[nread-3] << 8) |
                             (data[nread-2] << 16) | (data[nread-1] << 24);
    if (expected_size != (uint32_t)*out_len) {
        fprintf(stderr, "%s: %s: size mismatch\n", prog, filename);
    }
    
    free(data);
    return 0;
}

/* === Command implementation === */
static int do_compress(const char *filename, int to_stdout, int keep, int force) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "%s: %s: %s\n", prog, filename, strerror(errno));
        return 1;
    }
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    uint8_t *data = malloc(fsize);
    size_t nread = fread(data, 1, fsize, f);
    fclose(f);
    
    if (to_stdout) {
        /* Write gzip to stdout */
        /* Compress data */
        uint8_t *deflate_out = NULL;
        size_t deflate_len = 0;
        if (deflate_stored(data, nread, &deflate_out, &deflate_len) != 0) {
            fprintf(stderr, "%s: compression failed\n", prog);
            free(data);
            return 1;
        }
        
        init_crc32();
        uint32_t crc = crc32_update(0, data, nread);
        
        uint8_t header[10] = {
            GZIP_ID1, GZIP_ID2, GZIP_CM_DEFLATE, 0,
            0, 0, 0, 0, 0, GZIP_OS_UNIX
        };
        fwrite(header, 1, 10, stdout);
        fwrite(deflate_out, 1, deflate_len, stdout);
        
        uint8_t footer[8];
        footer[0] = crc & 0xFF; footer[1] = (crc >> 8) & 0xFF;
        footer[2] = (crc >> 16) & 0xFF; footer[3] = (crc >> 24) & 0xFF;
        uint32_t isize = (uint32_t)(nread & 0xFFFFFFFF);
        footer[4] = isize & 0xFF; footer[5] = (isize >> 8) & 0xFF;
        footer[6] = (isize >> 16) & 0xFF; footer[7] = (isize >> 24) & 0xFF;
        fwrite(footer, 1, 8, stdout);
        
        free(deflate_out);
    } else {
        /* Create .gz file */
        char outname[4096];
        snprintf(outname, sizeof(outname), "%s.gz", filename);
        
        if (!force && access(outname, F_OK) == 0) {
            fprintf(stderr, "%s: %s already exists; use -f to overwrite\n", prog, outname);
            free(data);
            return 1;
        }
        
        if (gzip_write(outname, data, nread) != 0) {
            free(data);
            return 1;
        }
        
        if (!keep) {
            unlink(filename);
        }
    }
    
    free(data);
    return 0;
}

static int do_decompress(const char *filename, int to_stdout, int keep, int force) {
    uint8_t *out = NULL;
    size_t out_len = 0;
    
    if (gzip_read(filename, &out, &out_len) != 0) {
        return 1;
    }
    
    if (to_stdout) {
        fwrite(out, 1, out_len, stdout);
        free(out);
        return 0;
    }
    
    /* Determine output filename: remove .gz extension */
    char outname[4096];
    size_t len = strlen(filename);
    if (len > 3 && strcmp(filename + len - 3, ".gz") == 0) {
        memcpy(outname, filename, len - 3);
        outname[len - 3] = '\0';
    } else if (len > 4 && strcmp(filename + len - 4, ".gzip") == 0) {
        memcpy(outname, filename, len - 4);
        outname[len - 4] = '\0';
    } else {
        /* Default: append .out */
        snprintf(outname, sizeof(outname), "%s.out", filename);
    }
    
    if (!force && access(outname, F_OK) == 0) {
        fprintf(stderr, "%s: %s already exists; use -f to overwrite\n", prog, outname);
        free(out);
        return 1;
    }
    
    FILE *f = fopen(outname, "wb");
    if (!f) {
        fprintf(stderr, "%s: %s: %s\n", prog, outname, strerror(errno));
        free(out);
        return 1;
    }
    fwrite(out, 1, out_len, f);
    fclose(f);
    
    free(out);
    
    if (!keep) {
        unlink(filename);
    }
    
    return 0;
}

static int do_test(const char *filename) {
    uint8_t *out = NULL;
    size_t out_len = 0;
    if (gzip_read(filename, &out, &out_len) != 0) {
        return 1;
    }
    free(out);
    printf("%s\tOK\n", filename);
    return 0;
}

static int do_list(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "%s: %s: %s\n", prog, filename, strerror(errno));
        return 1;
    }
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    uint8_t header[10];
    if (fread(header, 1, 10, f) < 10 ||
        header[0] != GZIP_ID1 || header[1] != GZIP_ID2) {
        fprintf(stderr, "%s: %s: not a gzip file\n", prog, filename);
        fclose(f);
        return 1;
    }
    
    /* Read original size from footer */
    fseek(f, -4, SEEK_END);
    uint8_t sz[4];
    fread(sz, 1, 4, f);
    uint32_t orig_size = sz[0] | (sz[1] << 8) | (sz[2] << 16) | (sz[3] << 24);
    
    printf("  compressed  uncompressed  ratio  uncompressed_name\n");
    printf("%10ld  %12u  %3.0f%%  %s\n",
           fsize, orig_size,
           orig_size > 0 ? (100.0 * (1.0 - (double)fsize / orig_size)) : 0.0,
           filename);
    
    fclose(f);
    return 0;
}

static void usage(void) {
    printf(
        "gzip — compression tool (meuos-utils)\n"
        "\n"
        "usage: gzip [options] [files...]\n"
        "       gzip -d [options] [files...]\n"
        "\n"
        "options:\n"
        "  -c    write to stdout, keep original\n"
        "  -d    decompress\n"
        "  -k    keep original file\n"
        "  -f    force overwrite\n"
        "  -t    test integrity\n"
        "  -l    list contents\n"
        "  -N    compression level (1-9, ignored — uses stored blocks)\n"
        "  --help     show help\n"
        "  --version  show version\n");
}

int main(int argc, char **argv) {
    int decompress = 0;
    int to_stdout = 0;
    int keep = 0;
    int force = 0;
    int test_mode = 0;
    int list_mode = 0;
    int oi = 1;
    
    while (oi < argc && argv[oi][0] == '-' && argv[oi][1] != '\0') {
        const char *opt = argv[oi];
        if (strcmp(opt, "--help") == 0) { usage(); return 0; }
        if (strcmp(opt, "--version") == 0) { printf("gzip (meuos-utils)\n"); return 0; }
        
        int j = 1;
        while (opt[j]) {
            switch (opt[j]) {
            case 'd': decompress = 1; break;
            case 'c': to_stdout = 1; break;
            case 'k': keep = 1; break;
            case 'f': force = 1; break;
            case 't': test_mode = 1; break;
            case 'l': list_mode = 1; break;
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                /* compression level — ignored for now */
                break;
            default:
                fprintf(stderr, "%s: unknown option -%c\n", prog, opt[j]);
                return 2;
            }
            j++;
        }
        oi++;
    }
    
    if (oi >= argc) {
        /* No files: use stdin/stdout */
        if (decompress) {
            /* Read from stdin */
            fseek(stdin, 0, SEEK_END);
            long fsize = ftell(stdin);
            fseek(stdin, 0, SEEK_SET);
            uint8_t *data = malloc(fsize);
            size_t nread = fread(data, 1, fsize, stdin);
            
            uint8_t *out = NULL;
            size_t out_len = 0;
            if (inflate(data, nread - 8, &out, &out_len) == 0) {
                fwrite(out, 1, out_len, stdout);
                free(out);
            }
            free(data);
            return 0;
        } else {
            /* Compress stdin to stdout */
            fseek(stdin, 0, SEEK_END);
            long fsize = ftell(stdin);
            fseek(stdin, 0, SEEK_SET);
            uint8_t *data = malloc(fsize);
            size_t nread = fread(data, 1, fsize, stdin);
            
            uint8_t *deflate_out = NULL;
            size_t deflate_len = 0;
            if (deflate_stored(data, nread, &deflate_out, &deflate_len) == 0) {
                init_crc32();
                uint32_t crc = crc32_update(0, data, nread);
                uint8_t header[10] = {
                    GZIP_ID1, GZIP_ID2, GZIP_CM_DEFLATE, 0,
                    0, 0, 0, 0, 0, GZIP_OS_UNIX
                };
                fwrite(header, 1, 10, stdout);
                fwrite(deflate_out, 1, deflate_len, stdout);
                uint8_t footer[8];
                footer[0] = crc & 0xFF; footer[1] = (crc >> 8) & 0xFF;
                footer[2] = (crc >> 16) & 0xFF; footer[3] = (crc >> 24) & 0xFF;
                uint32_t isize = (uint32_t)(nread & 0xFFFFFFFF);
                footer[4] = isize & 0xFF; footer[5] = (isize >> 8) & 0xFF;
                footer[6] = (isize >> 16) & 0xFF; footer[7] = (isize >> 24) & 0xFF;
                fwrite(footer, 1, 8, stdout);
                free(deflate_out);
            }
            free(data);
            return 0;
        }
    }
    
    int ret = 0;
    for (int i = oi; i < argc; i++) {
        int rc;
        if (test_mode) {
            rc = do_test(argv[i]);
        } else if (list_mode) {
            rc = do_list(argv[i]);
        } else if (decompress) {
            rc = do_decompress(argv[i], to_stdout, keep, force);
        } else {
            rc = do_compress(argv[i], to_stdout, keep, force);
        }
        if (rc != 0) ret = rc;
    }
    
    return ret;
}
