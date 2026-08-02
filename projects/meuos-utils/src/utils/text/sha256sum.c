/* sha256sum — 计算/校验文件的 SHA-256 校验和
 * 用法：sha256sum [FILE]...
 * 选项：-c 从 FILE 读取校验和并验证
 * 实现：FIPS 180-4 SHA-256
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"

/* === SHA-256 implementation (public domain) === */
typedef struct {
    uint32_t state[8];
    uint64_t bits;
    unsigned char buf[64];
    size_t buf_len;
} sha256_ctx;

static const uint32_t sha256_k[64] = {
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROTR(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define S0(x) (ROTR(x,2)^ROTR(x,13)^ROTR(x,22))
#define S1(x) (ROTR(x,6)^ROTR(x,11)^ROTR(x,25))
#define s0(x) (ROTR(x,7)^ROTR(x,18)^((x)>>3))
#define s1(x) (ROTR(x,17)^ROTR(x,19)^((x)>>10))

static void sha256_block(sha256_ctx *c, const unsigned char *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)p[i*4]<<24)|((uint32_t)p[i*4+1]<<16)|((uint32_t)p[i*4+2]<<8)|((uint32_t)p[i*4+3]);
    for (int i = 16; i < 64; i++)
        w[i] = s1(w[i-2]) + w[i-7] + s0(w[i-15]) + w[i-16];
    uint32_t a=c->state[0],b=c->state[1],cc=c->state[2],d=c->state[3];
    uint32_t e=c->state[4],f=c->state[5],g=c->state[6],h=c->state[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + S1(e) + ((e&f)^((~e)&g)) + sha256_k[i] + w[i];
        uint32_t t2 = S0(a) + ((a&b)^(a&cc)^(b&cc));
        h=g; g=f; f=e; e=d+t1; d=cc; cc=b; b=a; a=t1+t2;
    }
    c->state[0]+=a; c->state[1]+=b; c->state[2]+=cc; c->state[3]+=d;
    c->state[4]+=e; c->state[5]+=f; c->state[6]+=g; c->state[7]+=h;
}

static void sha256_init(sha256_ctx *c) {
    c->state[0]=0x6a09e667; c->state[1]=0xbb67ae85; c->state[2]=0x3c6ef372; c->state[3]=0xa54ff53a;
    c->state[4]=0x510e527f; c->state[5]=0x9b05688c; c->state[6]=0x1f83d9ab; c->state[7]=0x5be0cd19;
    c->bits=0; c->buf_len=0;
}

static void sha256_update(sha256_ctx *c, const void *data, size_t len) {
    const unsigned char *p = data;
    c->bits += (uint64_t)len * 8;
    size_t off = 0;
    if (c->buf_len > 0) {
        size_t need = 64 - c->buf_len;
        size_t take = len < need ? len : need;
        memcpy(c->buf + c->buf_len, p, take);
        c->buf_len += take; off += take;
        if (c->buf_len == 64) { sha256_block(c, c->buf); c->buf_len = 0; }
    }
    while (off + 64 <= len) { sha256_block(c, p + off); off += 64; }
    if (off < len) { memcpy(c->buf, p + off, len - off); c->buf_len = len - off; }
}

static void sha256_final(sha256_ctx *c, unsigned char out[32]) {
    /* 保存原始 bit 数，padding 之前取值 */
    uint64_t saved_bits = c->bits;
    unsigned char pad[64];
    memset(pad, 0, 64);
    pad[0] = 0x80;
    size_t padlen = c->buf_len < 56 ? 56 - c->buf_len : 120 - c->buf_len;
    sha256_update(c, pad, padlen);
    unsigned char lenbuf[8];
    for (int i = 0; i < 8; i++) lenbuf[i] = (unsigned char)(saved_bits >> ((7-i)*8));
    sha256_update(c, lenbuf, 8);
    for (int i = 0; i < 8; i++) {
        out[i*4]   = (unsigned char)(c->state[i] >> 24);
        out[i*4+1] = (unsigned char)(c->state[i] >> 16);
        out[i*4+2] = (unsigned char)(c->state[i] >> 8);
        out[i*4+3] = (unsigned char)(c->state[i]);
    }
}

/* sha256_hex 已由 libutils 的 bytes_to_hex 替代 */

static int sha256_file(const char *fname, char *hexout) {
    FILE *f = fopen(fname, "rb");
    if (!f) return -1;
    sha256_ctx c; sha256_init(&c);
    unsigned char buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) sha256_update(&c, buf, n);
    fclose(f);
    unsigned char hash[32]; sha256_final(&c, hash);
    bytes_to_hex(hash, 32, hexout);
    return 0;
}

int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    if (argi < argc && !strcmp(argv[argi], "--help")) { printf("Usage: sha256sum [-c] [FILE]...\n"); return 0; }
    int check = 0;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (char *p = argv[argi]+1; *p; p++) {
            if (*p == 'c') check = 1;
            else { fprintf(stderr, "sha256sum: unknown option -%c\n", *p); return 2; }
        }
        argi++;
    }
    if (check) {
        int rc = 0;
        for (int fi = argi; fi < argc; fi++) {
            FILE *f = fopen(argv[fi], "r");
            if (!f) { fprintf(stderr, "sha256sum: %s: %s\n", argv[fi], strerror(errno)); rc = 1; continue; }
            char line[600];
            while (fgets(line, sizeof(line), f)) {
                char expected[65];
                char fname[256];
                if (sscanf(line, "%64s %255s", expected, fname) < 2) continue;
                if (fname[0] == '*') memmove(fname, fname + 1, strlen(fname));
                char actual[65];
                if (sha256_file(fname, actual) < 0) {
                    printf("%s: FAILED\n", fname);
                    rc = 1; continue;
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
            sha256_ctx c; sha256_init(&c);
            unsigned char buf[65536]; size_t n;
            while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) sha256_update(&c, buf, n);
            unsigned char hash[32]; sha256_final(&c, hash);
            char hex[65]; bytes_to_hex(hash, 32, hex);
            printf("%s  -\n", hex);
        } else {
            char hex[65];
            if (sha256_file(argv[fi], hex) < 0) {
                fprintf(stderr, "sha256sum: %s: %s\n", argv[fi], strerror(errno));
                rc = 1;
            } else {
                printf("%s  %s\n", hex, argv[fi]);
            }
        }
    }
    return rc;
}
