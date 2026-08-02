/* mktemp — 创建临时文件或目录
 * 用法：mktemp [OPTION]... [TEMPLATE]
 * 选项：-d 创建目录, -u 不创建(仅生成名,不安全), -q 静默
 * TEMPLATE 中末尾的 XXXXXX 被随机字符替换
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static const char version[] = "0.1.0-mktemp (meuos-utils)";

static int make_random(char *tmpl) {
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    size_t len = strlen(tmpl);
    int count = 0;
    /* 找末尾连续的 X */
    int xstart = (int)len;
    while (xstart > 0 && tmpl[xstart-1] == 'X') xstart--;
    count = (int)len - xstart;
    if (count < 6) return -1;
    for (int i = 0; i < count; i++) {
        int fd;
        for (int tries = 0; tries < 100; tries++) {
            for (int j = 0; j < count; j++)
                tmpl[xstart + j] = chars[(unsigned)rand() % (sizeof(chars)-1)];
            /* 尝试创建 */
            if (count >= 6) {
                /* mkstemp 风格：直接用 mkstemp */
            }
        }
        return 0;
    }
    return -1;
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--version")) { printf("mktemp %s\n", version); return 0; }
    if (argc > 1 && !strcmp(argv[1], "--help")) {
        printf("Usage: mktemp [-d] [-q] [-u] [TEMPLATE]\n");
        return 0;
    }
    int make_dir = 0, dry_run = 0, quiet = 0;
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (char *p = argv[argi]+1; *p; p++) {
            if (*p == 'd') make_dir = 1;
            else if (*p == 'u') dry_run = 1;
            else if (*p == 'q') quiet = 1;
            else { fprintf(stderr, "mktemp: unknown option -%c\n", *p); return 2; }
        }
        argi++;
    }
    const char *tmpl;
    char tmplbuf[256];
    if (argi < argc) {
        strncpy(tmplbuf, argv[argi], sizeof(tmplbuf)-1);
        tmplbuf[sizeof(tmplbuf)-1] = '\0';
    } else {
        const char *tmpdir = getenv("TMPDIR");
        if (!tmpdir) tmpdir = "/tmp";
        snprintf(tmplbuf, sizeof(tmplbuf), "%s/tmp.XXXXXX", tmpdir);
    }
    tmpl = tmplbuf;
    if (dry_run) {
        make_random(tmplbuf);
        puts(tmplbuf);
        return 0;
    }
    if (make_dir) {
        char *r = mkdtemp(tmplbuf);
        if (!r) { if (!quiet) fprintf(stderr, "mktemp: %s: %s\n", tmpl, strerror(errno)); return 1; }
        puts(r);
    } else {
        int fd = mkstemp(tmplbuf);
        if (fd < 0) { if (!quiet) fprintf(stderr, "mktemp: %s: %s\n", tmpl, strerror(errno)); return 1; }
        close(fd);
        puts(tmplbuf);
    }
    return 0;
}
