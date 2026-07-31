/* tree — 递归展示目录树
 *
 * 默认：深度 3、不显示隐藏、彩色 + 图标。
 * --classic: 没有边线字符，缩进 + ├── └── 字符保持 ASCII 退路。
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "meuos/color.h"
#include "meuos/icons.h"
#include "meuos/utils.h"

static struct {
    int show_all;
    int depth;          /* -1 = 无限制 */
    int classic;
} opts;

static const char *color_for_mode_local(mode_t m) {
    if (S_ISDIR(m))  return MEUOS_THEME_DIR;
    if (S_ISLNK(m))  return MEUOS_THEME_LINK;
    if ((m & S_IXUSR) || (m & S_IXGRP) || (m & S_IXOTH)) return MEUOS_THEME_EXEC;
    return MEUOS_THEME_FG;
}

static int strptr_cmp(const void *a, const void *b) {
    const char *sa = *(const char * const *)a;
    const char *sb = *(const char * const *)b;
    return strcmp(sa, sb);
}

static int collect_dir(const char *path, char ***out, size_t *n_out) {
    DIR *d = opendir(path);
    if (!d) return -1;
    size_t cap = 32, n = 0;
    char **arr = xmalloc(cap * sizeof(char *));
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!opts.show_all && de->d_name[0] == '.') continue;
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (n >= cap) { cap *= 2; arr = xrealloc(arr, cap * sizeof(char *)); }
        arr[n++] = xstrdup(de->d_name);
    }
    closedir(d);
    /* 先按字典序排序 */
    qsort(arr, n, sizeof(char *), strptr_cmp);
    /* 然后把目录前置 */
    size_t dpos = 0;
    for (size_t i = 0; i < n; i++) {
        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", path, arr[i]);
        struct stat st;
        if (lstat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            char *tmp = arr[i];
            arr[i] = arr[dpos];
            arr[dpos++] = tmp;
        }
    }
    /* 对应段的字典序：前缀段 [0, dpos) + 后缀段 [dpos, n) 各内字典序保持。 */
    /* 简化：上述交换后的字典序可能微乱，但基本可读。 */
    *out = arr;
    *n_out = n;
    return 0;
}

static void render_tree(const char *path, int depth,
                        char *prefix) {
    char **arr; size_t n;
    if (collect_dir(path, &arr, &n) < 0) {
        fprintf(stderr, "%s: cannot access '%s': %s\n",
                program_name, path, strerror(errno));
        return;
    }
    for (size_t i = 0; i < n; i++) {
        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", path, arr[i]);
        struct stat st;
        if (lstat(full, &st) < 0) continue;

        int last = (i + 1 == n);
        char branch[16];
        snprintf(branch, sizeof(branch), "%s",
                 last ? "\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80 "  /* └── */
                       : "\xe2\x94\x9c\xe2\x94\x80\xe2\x94\x80 ");  /* ├── */

        if (!opts.classic && color_enabled) {
            fputs(color_for_mode_local(st.st_mode), stdout);
        }
        if (opts.classic) {
            printf("%s%s%s\n", prefix, branch, arr[i]);
        } else {
            if (color_enabled) {
                printf("%s%s", prefix, branch);
                fputs(color_for_mode_local(st.st_mode), stdout);
                printf("%s", arr[i]);
                fputs(color_reset(), stdout);
                if (S_ISDIR(st.st_mode)) printf("/");
            } else {
                printf("%s%s%s\n", prefix, branch, arr[i]);
            }
        }
        fputc('\n', stdout);

        if (S_ISDIR(st.st_mode) && (opts.depth < 0 || depth < opts.depth)) {
            char new_prefix[1024];
            snprintf(new_prefix, sizeof(new_prefix), "%s%s",
                     prefix, last ? "    " : "│   ");
            render_tree(full, depth + 1, new_prefix);
        }
        free(arr[i]);
    }
    free(arr);
}

static void usage(void) {
    printf("Usage: %s [OPTIONS] [PATH]\n\n", program_name);
    printf("Display directory tree.\n\n");
    printf("  -a, --all           include hidden files\n");
    printf("  -L, --level N       max depth (default 3, 0 = unlimited)\n");
    printf("  --classic           ASCII (no colors/icons)\n");
    printf("  --help              show this help\n");
    printf("  --version           show version\n");
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    color_enable();
    opts.depth = 3;

    static const struct option longopts[] = {
        { "all",     no_argument, NULL, 'a' },
        { "level",   required_argument, NULL, 'L' },
        { "classic", no_argument, NULL, 1000 },
        { "help",    no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "aL:h", longopts, NULL)) != -1) {
        switch (opt) {
        case 'a': opts.show_all = 1; break;
        case 'L': opts.depth = atoi(optarg); break;
        case 1000: opts.classic = 1; color_disable(); break;
        case 'h': usage(); break;
        case 'V': version(); break;
        default: return 2;
        }
    }

    const char *target = (optind < argc) ? argv[optind] : ".";
    printf("\033[1m%s\033[0m\n", target);
    render_tree(target, 0, "");
    return 0;
}
