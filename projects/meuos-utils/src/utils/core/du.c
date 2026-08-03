/* du — 磁盘使用量统计
 * 用法：du [OPTION]... [FILE]...
 * 选项：-h 人类可读, -s 仅汇总, -k KB, -m MB, -b 字节, -a 所有文件
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "meuos/utils.h"


static int human = 0, summary = 0, all_files = 0;
static int unit = 512;  /* 默认块大小（POSIX: 512B blocks via stat_blocks） */

static long long count_dir(const char *path) {
    struct stat st;
    if (lstat(path, &st) < 0) {
        fprintf(stderr, "du: %s: %s\n", path, strerror(errno));
        return 0;
    }
    long long total = 0;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
                char child[4096];
                snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
                total += count_dir(child);
            }
            closedir(d);
        }
        if (all_files) {
            /* 报告当前目录 */
        }
    } else {
        total = st.st_blocks * 512;  /* st_blocks 始终是 512B 块 */
    }
    if (!summary && S_ISDIR(st.st_mode) && !all_files) {
        /* 报告子目录汇总后，报告自身 */
    }
    if (!summary || S_ISDIR(st.st_mode)) {
        long long report;
        if (human) {
            /* 简化输出 */
            report = total;
            if (report >= 1073741824) printf("%4.1fG\t%s\n", (double)report/1073741824, path);
            else if (report >= 1048576) printf("%4.1fM\t%s\n", (double)report/1048576, path);
            else if (report >= 1024) printf("%4.1fK\t%s\n", (double)report/1024, path);
            else printf("%4lldB\t%s\n", report, path);
        } else {
            report = (total + unit - 1) / unit;  /* 向上取整到块 */
            printf("%lld\t%s\n", report, path);
        }
    }
    return total;
}

int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    if (argi < argc && !strcmp(argv[argi], "--help")) { printf("Usage: du [-h] [-s] [-a] [-k|-m|-b] [FILE]...\n"); return 0; }
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        for (char *p = argv[argi]+1; *p; p++) {
            switch (*p) {
            case 'h': human = 1; break;
            case 's': summary = 1; break;
            case 'a': all_files = 1; break;
            case 'k': unit = 1024; break;
            case 'm': unit = 1048576; break;
            case 'b': unit = 1; break;
            default: fprintf(stderr, "du: unknown option -%c\n", *p); return 2;
            }
        }
        argi++;
    }
    if (argi >= argc) {
        count_dir(".");
    } else {
        for (int i = argi; i < argc; i++)
            count_dir(argv[i]);
    }
    return 0;
}
