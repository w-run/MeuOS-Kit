/* md5sum — 计算/校验文件的 MD5 校验和
 * 用法：md5sum [FILE]...
 * 选项：-c 从 FILE 读取校验和并验证, -b 二进制模式(等同文本)
 * 实现：RFC 1321 MD5
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"

/* === MD5 implementation (public domain) === */
typedef struct {
    uint32_t a, b, c, d;
    uint64_t bits;
    unsigned char buf[64];
    size_t buf_len;
} md5_ctx;

#define F(x,y,z) (((x)&(y))|((~(x))&(z)))
#define G(x,y,z) (((x)&(z))|((y)&(~(z))))
#define H(x,y,z) ((x)^(y)^(z))
#define I(x,y,z) ((y)^((x)|(~(z))))
#define ROTL(x,n) (((x)<<(n))|((x)>>(32-(n))))

static const uint32_t k[64] = {
0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
};

static const int r[64] = {
7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
};

static void md5_block(md5_ctx *c, const unsigned char *p) {
    uint32_t w[16];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i*4] | ((uint32_t)p[i*4+1]<<8) | ((uint32_t)p[i*4+2]<<16) | ((uint32_t)p[i*4+3]<<24);
    uint32_t a=c->a,b=c->b,cc=c->c,d=c->d;
    for (int i = 0; i < 64; i++) {
        uint32_t f; int g;
        if (i < 16) { f = F(b,cc,d); g = i; }
        else if (i < 32) { f = G(b,cc,d); g = (5*i+1)%16; }
        else if (i < 48) { f = H(b,cc,d); g = (3*i+5)%16; }
        else { f = I(b,cc,d); g = (7*i)%16; }
        uint32_t tmp = d; d = cc; cc = b;
        b = b + ROTL(a + f + k[i] + w[g], r[i]);
        a = tmp;
    }
    c->a += a; c->b += b; c->c += cc; c->d += d;
}

static void md5_init(md5_ctx *c) {
    c->a = 0x67452301; c->b = 0xefcdab89; c->c = 0x98badcfe; c->d = 0x10325476;
    c->bits = 0; c->buf_len = 0;
}

static void md5_update(md5_ctx *c, const void *data, size_t len) {
    const unsigned char *p = data;
    c->bits += (uint64_t)len * 8;
    size_t off = 0;
    if (c->buf_len > 0) {
        size_t need = 64 - c->buf_len;
        size_t take = len < need ? len : need;
        memcpy(c->buf + c->buf_len, p, take);
        c->buf_len += take; off += take;
        if (c->buf_len == 64) { md5_block(c, c->buf); c->buf_len = 0; }
    }
    while (off + 64 <= len) { md5_block(c, p + off); off += 64; }
    if (off < len) { memcpy(c->buf, p + off, len - off); c->buf_len = len - off; }
}

static void md5_final(md5_ctx *c, unsigned char out[16]) {
    /* 保存原始 bit 数，padding 之前取值 */
    uint64_t saved_bits = c->bits;
    unsigned char pad[64];
    memset(pad, 0, 64);
    pad[0] = 0x80;
    size_t padlen = c->buf_len < 56 ? 56 - c->buf_len : 120 - c->buf_len;
    md5_update(c, pad, padlen);
    unsigned char lenbuf[8];
    for (int i = 0; i < 8; i++) lenbuf[i] = (unsigned char)(saved_bits >> (i * 8));
    md5_update(c, lenbuf, 8);
    /* 输出 a/b/c/d 各 4 字节，小端 */
    for (int i = 0; i < 4; i++) {
        out[i]    = (unsigned char)(c->a >> (i*8));
        out[4+i]  = (unsigned char)(c->b >> (i*8));
        out[8+i]  = (unsigned char)(c->c >> (i*8));
        out[12+i] = (unsigned char)(c->d >> (i*8));
    }
}

/* md5_hex 已由 libutils 的 bytes_to_hex 替代 */

static int md5_file(const char *fname, char *hexout) {
    FILE *f = fopen(fname, "rb");
    if (!f) return -1;
    md5_ctx c; md5_init(&c);
    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) md5_update(&c, buf, n);
    fclose(f);
    unsigned char hash[16];
    md5_final(&c, hash);
    bytes_to_hex(hash, 16, hexout);
    return 0;
}

int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    if (argi < argc && !strcmp(argv[argi], "--help")) { printf("Usage: md5sum [-c] [FILE]...\n"); return 0; }
    int check = 0;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (char *p = argv[argi]+1; *p; p++) {
            if (*p == 'c') check = 1;
            else if (*p == 'b') {} /* binary mode, ignored */
            else { fprintf(stderr, "md5sum: unknown option -%c\n", *p); return 2; }
        }
        argi++;
    }
    if (check) {
        int rc = 0;
        for (int fi = argi; fi < argc; fi++) {
            FILE *f = fopen(argv[fi], "r");
            if (!f) { fprintf(stderr, "md5sum: %s: %s\n", argv[fi], strerror(errno)); rc = 1; continue; }
            char line[512];
            while (fgets(line, sizeof(line), f)) {
                char expected[33];
                char fname[256];
                /* 格式: <hash>  <file> */
                if (sscanf(line, "%32s %255s", expected, fname) < 2) continue;
                if (fname[0] == '*') memmove(fname, fname + 1, strlen(fname));
                char actual[33];
                if (md5_file(fname, actual) < 0) {
                    printf("%s: FAILED\n", fname);
                    rc = 1;
                    continue;
                }
                if (!strcmp(expected, actual)) printf("%s: OK\n", fname);
                else { printf("%s: FAILED\n", fname); rc = 1; }
            }
            fclose(f);
        }
        return rc;
    }
    if (argi >= argc) { argv[argi] = "-"; argc = argi + 1; }
    int rc = 0;
    for (int fi = argi; fi < argc; fi++) {
        if (!strcmp(argv[fi], "-")) {
            md5_ctx c; md5_init(&c);
            unsigned char buf[65536]; size_t n;
            while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) md5_update(&c, buf, n);
            unsigned char hash[16]; md5_final(&c, hash);
            char hex[33]; bytes_to_hex(hash, 16, hex);
            printf("%s  -\n", hex);
        } else {
            char hex[33];
            if (md5_file(argv[fi], hex) < 0) {
                fprintf(stderr, "md5sum: %s: %s\n", argv[fi], strerror(errno));
                rc = 1;
            } else {
                printf("%s  %s\n", hex, argv[fi]);
            }
        }
    }
    return rc;
}
