/* truncate — 将文件截断/扩展到指定大小
 * 用法：truncate [OPTION]... -s SIZE FILE...
 * 选项：-s SIZE 指定大小, -c 不创建文件, -r REFFILE 参照文件大小
 * SIZE 可加单位: K M G (1024), KB MB GB (1000)
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static const char version[] = "0.1.0-truncate (meuos-utils)";

static long long parse_size(const char *s) {
    char *end;
    double val = strtod(s, &end);
    if (end == s) return -1;
    if (*end == '\0') return (long long)val;
    if (!strcmp(end, "K") || !strcmp(end, "KiB")) return (long long)(val * 1024);
    if (!strcmp(end, "M") || !strcmp(end, "MiB")) return (long long)(val * 1048576);
    if (!strcmp(end, "G") || !strcmp(end, "GiB")) return (long long)(val * 1073741824LL);
    if (!strcmp(end, "KB")) return (long long)(val * 1000);
    if (!strcmp(end, "MB")) return (long long)(val * 1000000);
    if (!strcmp(end, "GB")) return (long long)(val * 1000000000LL);
    if (*end == '+' || *end == '-' || *end == '<' || *end == '/') {
        /* 前缀修饰: +N 增加, -N 减少, <N 至多, /N 向下取整 */
        return (long long)val;  /* 简化：返回数值部分 */
    }
    return -1;
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--version")) { printf("truncate %s\n", version); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--help")) {
        printf("Usage: truncate [-s SIZE] [-c] [-r REF] FILE...\n");
        return 0;
    }
    long long size = -1;
    int no_create = 0;
    const char *reffile = NULL;
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (!strcmp(argv[argi], "-s") && argi + 1 < argc) { size = parse_size(argv[++argi]); argi++; }
        else if (!strcmp(argv[argi], "-c")) { no_create = 1; argi++; }
        else if (!strcmp(argv[argi], "-r") && argi + 1 < argc) { reffile = argv[++argi]; argi++; }
        else if (argv[argi][1] == 's' && argv[argi][2] != '\0') { size = parse_size(argv[argi]+2); argi++; }
        else break;
    }
    if (size < 0 && !reffile) { fprintf(stderr, "truncate: must specify -s SIZE\n"); return 2; }
    if (reffile) {
        struct stat st;
        if (stat(reffile, &st) < 0) { fprintf(stderr, "truncate: %s: %s\n", reffile, strerror(errno)); return 1; }
        size = st.st_size;
    }
    if (argi >= argc) { fprintf(stderr, "truncate: missing file operand\n"); return 2; }
    int rc = 0;
    for (int i = argi; i < argc; i++) {
        if (truncate(argv[i], (off_t)size) < 0) {
            if (errno == ENOENT && !no_create) {
                /* 创建新文件 */
                int fd = open(argv[i], O_WRONLY | O_CREAT, 0644);
                if (fd < 0) { fprintf(stderr, "truncate: %s: %s\n", argv[i], strerror(errno)); rc = 1; continue; }
                if (ftruncate(fd, (off_t)size) < 0) {
                    fprintf(stderr, "truncate: %s: %s\n", argv[i], strerror(errno)); rc = 1;
                }
                close(fd);
            } else {
                fprintf(stderr, "truncate: %s: %s\n", argv[i], strerror(errno)); rc = 1;
            }
        }
    }
    return rc;
}
