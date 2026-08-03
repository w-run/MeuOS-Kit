/* base64 — Base64 编码/解码
 * 用法：base64 [OPTION]... [FILE]
 * 选项：-d 解码, -i 忽略非字母字符, -w COLS 每行列宽(0=不换行)
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "meuos/utils.h"

static const char b64tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void encode(FILE *in, int cols) {
    unsigned char buf[3];
    int col = 0;
    size_t n;
    while ((n = fread(buf, 1, 3, in)) > 0) {
        unsigned int v = (buf[0] << 16) | (n > 1 ? buf[1] << 8 : 0) | (n > 2 ? buf[2] : 0);
        putchar(b64tab[v >> 18]);
        putchar(b64tab[(v >> 12) & 0x3f]);
        if (n > 1) putchar(b64tab[(v >> 6) & 0x3f]);
        else putchar('=');
        if (n > 2) putchar(b64tab[v & 0x3f]);
        else putchar('=');
        if (cols > 0) {
            col += 4;
            if (col >= cols) { putchar('\n'); col = 0; }
        }
    }
    if (cols > 0 && col > 0) putchar('\n');
}

static void decode(FILE *in, int ignore_garbage) {
    int8_t b64val[256];
    memset(b64val, -1, sizeof(b64val));
    for (int i = 0; i < 64; i++) b64val[(unsigned char)b64tab[i]] = i;
    b64val['='] = 0;
    int buf[4];
    int count = 0;
    int c;
    while ((c = fgetc(in)) != EOF) {
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        if (c == '=') {
            buf[count++] = -2;
        } else if (b64val[(unsigned char)c] >= 0) {
            buf[count++] = b64val[(unsigned char)c];
        } else if (ignore_garbage) {
            continue;
        } else {
            fprintf(stderr, "base64: invalid input\n");
            exit(1);
        }
        if (count == 4) {
            unsigned int v = (buf[0] << 18) | (buf[1] << 12) | (buf[2] << 6) | buf[3];
            putchar((v >> 16) & 0xff);
            if (buf[2] != -2) putchar((v >> 8) & 0xff);
            if (buf[3] != -2) putchar(v & 0xff);
            count = 0;
        }
    }
}

int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    if (argi < argc && !strcmp(argv[argi], "--help")) { printf("Usage: base64 [-d] [-i] [-w COLS] [FILE]\n"); return 0; }
    int decode_mode = 0, ignore_garbage = 0, cols = 76;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (char *p = argv[argi]+1; *p; p++) {
            if (*p == 'd') decode_mode = 1;
            else if (*p == 'i') ignore_garbage = 1;
            else if (*p == 'w') {
                if (p[1] >= '0' && p[1] <= '9') { cols = atoi(p+1); break; }
                else if (argi + 1 < argc) { cols = atoi(argv[++argi]); break; }
            }
            else { fprintf(stderr, "base64: unknown option -%c\n", *p); return 2; }
        }
        argi++;
    }
    FILE *in;
    if (argi < argc) {
        if (!strcmp(argv[argi], "-")) in = stdin;
        else { in = fopen(argv[argi], "rb"); if (!in) { fprintf(stderr, "base64: %s: %s\n", argv[argi], strerror(errno)); return 1; } }
    } else in = stdin;
    if (decode_mode) decode(in, ignore_garbage);
    else encode(in, cols);
    if (in != stdin) fclose(in);
    return 0;
}
