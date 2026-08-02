/* tac — 逆序输出文件行（cat 的反向版）
 * 用法：tac [FILE]...
 * 选项：-s STRING 用 STRING 作为分隔符而非换行, -r 把分隔符当正则
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static const char version[] = "0.1.0-tac (meuos-utils)";

static char *read_all(FILE *fp, size_t *out_len) {
    size_t cap = 65536, len = 0;
    char *buf = malloc(cap);
    size_t n;
    while ((n = fread(buf + len, 1, cap - len, fp)) > 0) {
        len += n;
        if (len >= cap) { cap *= 2; buf = realloc(buf, cap); }
    }
    *out_len = len;
    return buf;
}

static void tac_buf(const char *data, size_t len, const char *sep) {
    size_t seplen = strlen(sep);
    /* 从后往前找分隔符 */
    size_t end = len;
    for (size_t i = len; i > 0; ) {
        i--;
        if (i + seplen <= len && memcmp(data + i, sep, seplen) == 0) {
            /* 输出 i+seplen..end */
            fwrite(data + i + seplen, 1, end - (i + seplen), stdout);
            fputs(sep, stdout);
            end = i;
        }
    }
    /* 输出最前面的部分 */
    if (end > 0) fwrite(data, 1, end, stdout);
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--version")) { printf("tac %s\n", version); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--help")) { printf("Usage: tac [-s SEP] [FILE]...\n"); return 0; }
    const char *sep = "\n";
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (!strcmp(argv[argi], "-s") && argi + 1 < argc) { sep = argv[++argi]; argi++; }
        else break;
    }
    if (argi >= argc) {
        /* stdin */
        size_t len;
        char *data = read_all(stdin, &len);
        tac_buf(data, len, sep);
        free(data);
    } else {
        for (int i = argi; i < argc; i++) {
            FILE *fp = fopen(argv[i], "r");
            if (!fp) { fprintf(stderr, "tac: %s: %s\n", argv[i], strerror(errno)); continue; }
            size_t len;
            char *data = read_all(fp, &len);
            fclose(fp);
            tac_buf(data, len, sep);
            free(data);
        }
    }
    return 0;
}
