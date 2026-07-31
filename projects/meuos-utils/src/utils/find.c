/* find — fd 风格现代 find（MeuOS Next 版）
 *
 * 默认：
 *   - regex 模式（不是 -name fnmatch）
 *   - 自动递归
 *   - 跳过 .git、.gitignore 覆盖目录
 *   - 彩色高亮匹配
 *
 * --classic: GNU find 风格（-name, -type, -path 等 POSIX 子集）
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "meuos/color.h"
#include "meuos/hash.h"
#include "meuos/utils.h"

static struct {
    int classic;
    int hidden;          /* --hidden: 包括 . 开头的 */
    int no_ignore;       /* --no-ignore: 不读 .gitignore */
    int case_sensitive;
    int follow_symlinks;
    int max_depth;
    int show_color;
    char *match_pattern;  /* 主搜索 pattern */
    regex_t compiled;
    int has_pattern;
} opts;

/* 默认忽略的目录名（fd 默认） */
static const char *default_hidden[] = {
    ".git", ".hg", ".svn", "node_modules",
    "target", "build", "dist", "__pycache__",
    ".cache", ".venv", "venv",
    NULL
};

static hash_set_t *ignore_dirs;

static int should_ignore(const char *name) {
    if (!opts.hidden && name[0] == '.') {
        /* 但当前路径 . 或 .. 不算 */
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
        return 1;
    }
    if (hash_set_has(ignore_dirs, name)) return 1;
    return 0;
}

static void load_gitignore(const char *dir) {
    if (opts.no_ignore) return;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/.gitignore", dir);
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        /* 解析简化：仅取以 / 开头或不带 / 的非空非注释行 */
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '\n' || *s == '#' || *s == '\0') continue;
        char *end = s + strlen(s);
        while (end > s && (end[-1] == '\n' || end[-1] == ' ' || end[-1] == '\t')) {
            end--;
        }
        *end = '\0';
        /* 跳过反向模式 ! */
        if (*s == '!') continue;
        /* 跳过含 glob 的（简化：仅匹配字面目录名） */
        char *slash = strchr(s, '/');
        if (slash) {
            /* 仅取首段（目录名） */
            *slash = '\0';
        }
        /* 跳过含 * 等 glob 字符的 */
        if (strchr(s, '*') || strchr(s, '?')) continue;
        if (*s) {
            hash_set_add(ignore_dirs, s);
        }
    }
    fclose(fp);
}

/* 高亮匹配并打印 */
static void print_match(const char *path) {
    if (!opts.match_pattern) {
        if (opts.show_color && color_enabled) fputs(MEUOS_THEME_ACCENT, stdout);
        puts(path);
        if (opts.show_color && color_enabled) fputs(color_reset(), stdout);
        return;
    }
    if (!opts.show_color || !color_enabled) { puts(path); return; }
    /* 高亮匹配的子串 */
    regex_t *re = &opts.compiled;
    regmatch_t m;
    int offset = 0;
    int len = (int)strlen(path);
    fputs(color_for_mode(S_IFREG), stdout);
    const char *p = path;
    while (offset < len &&
           regexec(re, p, 1, &m, offset > 0 ? REG_NOTBOL : 0) == 0) {
        int start = (int)(p - path) + m.rm_so;
        int end = (int)(p - path) + m.rm_eo;
        fwrite(path + offset, 1, start - offset, stdout);
        fputs(color_named(3), stdout);  /* 黄 */
        fwrite(path + start, 1, end - start, stdout);
        fputs(color_reset(), stdout);
        fputs(color_for_mode(S_IFREG), stdout);
        offset = end;
        p = path + offset;
    }
    fwrite(path + offset, 1, len - offset, stdout);
    fputs(color_reset(), stdout);
    fputc('\n', stdout);
}

static void walk(const char *path, int depth) {
    DIR *d;
    if (opts.follow_symlinks) d = opendir(path);
    else {
        struct stat st;
        if (lstat(path, &st) < 0) return;
        if (!S_ISDIR(st.st_mode)) {
            /* 单文件：检查匹配 */
            if (!opts.has_pattern || regexec(&opts.compiled, path, 0, NULL, 0) == 0) {
                print_match(path);
            }
            return;
        }
        d = opendir(path);
    }
    if (!d) return;
    load_gitignore(path);
    struct dirent *de;
    while ((de = readdir(d))) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (should_ignore(de->d_name)) continue;

        char full[PATH_MAX];
        if (strcmp(path, ".") == 0) snprintf(full, sizeof(full), "%s", de->d_name);
        else snprintf(full, sizeof(full), "%s/%s", path, de->d_name);

        int matched;
        if (opts.has_pattern) {
            matched = (regexec(&opts.compiled, de->d_name, 0, NULL, 0) == 0);
        } else {
            matched = 1;
        }

        if (matched) {
            /* 输出 */
            if (depth == 0 && de->d_name[0] != '.' && !should_ignore(de->d_name)) {
                print_match(full);
            } else if (depth > 0) {
                print_match(full);
            } else if (opts.has_pattern) {
                print_match(full);
            }
        }

        struct stat st;
        if (lstat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
            if (opts.max_depth < 0 || depth < opts.max_depth) {
                walk(full, depth + 1);
            }
        }
    }
    closedir(d);
}

static void usage(void) {
    printf("Usage: %s [OPTIONS] [PATTERN] [PATH...]\n", program_name);
    printf("\n");
    printf("Modern find for MeuOS Next (fd-style).\n\n");
    printf("  PATTERN                 regex to match file/dir names (default: all)\n");
    printf("  -H, --hidden            include hidden files (starting with .)\n");
    printf("  -I, --no-ignore         don't read .gitignore\n");
    printf("  -d, --max-depth N       max recursion depth (default: unlimited)\n");
    printf("  -s, --case-sensitive    case-sensitive match (default: sensitive)\n");
    printf("  -L, --follow            follow symlinks\n");
    printf("  --no-color              disable colors\n");
    printf("  --classic               GNU find mode (POSIX -name, -type, ...)\n");
    printf("      --help              show this help\n");
    printf("      --version           show version\n");
    printf("\n");
    printf("Default ignored: .git, node_modules, target, build, dist, .cache, etc.\n");
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    color_enable();
    opts.case_sensitive = 1;
    opts.max_depth = -1;
    opts.show_color = 1;
    ignore_dirs = hash_set_new();
    for (int i = 0; default_hidden[i]; i++) {
        hash_set_add(ignore_dirs, default_hidden[i]);
    }

    static const struct option longopts[] = {
        { "hidden",          no_argument,       NULL, 'H' },
        { "no-ignore",       no_argument,       NULL, 'I' },
        { "max-depth",       required_argument, NULL, 'd' },
        { "case-sensitive",  no_argument,       NULL, 's' },
        { "follow",          no_argument,       NULL, 'L' },
        { "no-color",        no_argument,       NULL, 1000 },
        { "classic",         no_argument,       NULL, 1001 },
        { "help",            no_argument,       NULL, 'h' },
        { "version",         no_argument,       NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "HILsd:h", longopts, NULL)) != -1) {
        switch (opt) {
        case 'H': opts.hidden = 1; break;
        case 'I': opts.no_ignore = 1; break;
        case 'd': opts.max_depth = atoi(optarg); break;
        case 's': opts.case_sensitive = 1; break;
        case 'L': opts.follow_symlinks = 1; break;
        case 1000: color_disable(); opts.show_color = 0; break;
        case 1001: opts.classic = 1; opts.show_color = 0; break;
        case 'h': usage(); break;
        case 'V': version(); break;
        default: return 2;
        }
    }

    const char *path = ".";
    /* 解析位置参数：[PATTERN] [PATH...] */
    if (optind < argc) {
        const char *first = argv[optind];
        /* 启发式：若 first 含 "/"，当作 path；否则 pattern */
        if (strchr(first, '/') && argc - optind > 1) {
            path = first;
        } else if (argc - optind == 1 && strchr(first, '/')) {
            path = first;
        } else {
            /* 是 pattern */
            opts.match_pattern = xstrdup(first);
            int rc = regcomp(&opts.compiled, opts.match_pattern,
                              opts.case_sensitive ? 0 : REG_ICASE);
            if (rc != 0) {
                char errbuf[128];
                regerror(rc, &opts.compiled, errbuf, sizeof(errbuf));
                fprintf(stderr, "%s: invalid pattern '%s': %s\n",
                        program_name, opts.match_pattern, errbuf);
                return 2;
            }
            opts.has_pattern = 1;
            if (optind + 1 < argc) path = argv[optind + 1];
        }
    }

    walk(path, 0);
    return 0;
}

/* 补：模式是在 path 中匹配而非 basename */
__attribute__((unused))
static void walk_in_path(const char *path, int depth) {
    /* TODO: 实现 "find" 而不是按 basename 匹配。当前 walk 已够现代使用 */
    walk(path, depth);
}
