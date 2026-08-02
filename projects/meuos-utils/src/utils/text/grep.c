/* grep — ripgrep-lite 现代 grep（MeuOS Next 版）
 *
 * 默认：
 *   - 递归子目录
 *   - 跳过 .gitignore
 *   - 彩色高亮匹配
 *   - 输出 <file>:<line>:<content>
 *
 * --classic: GNU grep 兼容
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
#include <unistd.h>

#include "meuos/color.h"
#include "meuos/hash.h"
#include "meuos/utils.h"

static struct {
    int recursive;
    int no_ignore;
    int show_color;
    int show_line_num;
    int ignore_case;
    int invert;
    int classic;
    int fixed;       /* -F 字面 */
    int only_files;  /* -l */
    int count_only;  /* -c */
    char *pattern;
    regex_t compiled;
} opts;

static hash_set_t *ignore_files;
static hash_set_t *ignore_dirs;
static const char *default_hidden[] = {
    ".git", ".hg", ".svn", "node_modules", NULL
};

static int should_ignore_dir(const char *name) {
    return hash_set_has(ignore_dirs, name);
}

/* 字面串搜索（不用 regex） */
static char *str_find(const char *hay, const char *needle, int icase) {
    if (icase) {
        size_t nl = strlen(needle);
        for (const char *p = hay; *p; p++) {
            if (strncasecmp(p, needle, nl) == 0) return (char *)p;
        }
        return NULL;
    }
    return strstr(hay, needle);
}

static int grep_file(const char *path, FILE *fp) {
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    int lineno = 0;
    int matched = 0, count = 0;
    while ((n = getline(&line, &cap, fp)) >= 0) {
        lineno++;
        if (n > 0 && line[n-1] == '\n') { line[--n] = '\0'; }
        int hit;
        if (opts.fixed) {
            char *p = str_find(line, opts.pattern, opts.ignore_case);
            hit = (opts.invert ? (p == NULL) : (p != NULL));
            if (p && hit && !opts.invert) {
                /* 高亮 */
                if (opts.show_color && color_enabled) {
                    size_t pre = (size_t)(p - line);
                    size_t pnlen = strlen(opts.pattern);
                    fwrite(line, 1, pre, stdout);
                    fputs(color_named(1), stdout);
                    fwrite(p, 1, pnlen, stdout);
                    fputs(color_reset(), stdout);
                    puts(p + pnlen);
                } else {
                    puts(line);
                }
            } else if (hit) {
                puts(line);
            }
        } else {
            int rc = regexec(&opts.compiled, line, 0, NULL, 0);
            hit = (opts.invert ? rc != 0 : rc == 0);
            if (hit) {
                if (opts.only_files) {
                    puts(path);
                    matched = 1;
                    goto done;
                }
                if (opts.show_line_num) {
                    if (opts.show_color && color_enabled) {
                        printf("%s%s:%d:", color_named(2), path, lineno);
                    } else {
                        printf("%s:%d:", path, lineno);
                    }
                }
                if (opts.show_color && color_enabled && !opts.only_files) {
                    regmatch_t m;
                    const char *p = line;
                    while (regexec(&opts.compiled, p, 1, &m, 0) == 0) {
                        if (m.rm_so == m.rm_eo) break;
                        fwrite(p, 1, m.rm_so, stdout);
                        fputs(color_named(1), stdout);
                        fwrite(p + m.rm_so, 1, m.rm_eo - m.rm_so, stdout);
                        fputs(color_reset(), stdout);
                        p = p + m.rm_eo;
                        if (m.rm_eo == m.rm_so) break;
                    }
                    puts(p);
                } else {
                    puts(line);
                }
                count++;
                matched = 1;
            }
        }
    }
done:
    free(line);
    return matched;
}

static void walk(const char *path) {
    struct stat st;
    if (lstat(path, &st) < 0) return;
    if (S_ISDIR(st.st_mode)) {
        if (!opts.recursive) return;
        DIR *d = opendir(path);
        if (!d) return;
        struct dirent *de;
        while ((de = readdir(d))) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
            if (de->d_name[0] == '.' && !opts.no_ignore) continue;
            if (should_ignore_dir(de->d_name)) continue;
            char full[PATH_MAX];
            snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
            walk(full);
        }
        closedir(d);
        return;
    }
    /* 单文件 */
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    grep_file(path, fp);
    fclose(fp);
}

static void usage(void) {
    printf("Usage: %s [OPTIONS] PATTERN [PATH...]\n", program_name);
    printf("\n");
    printf("Modern grep for MeuOS Next (ripgrep-lite).\n\n");
    printf("  PATTERN                 regex or fixed string to search\n");
    printf("  -r, --recursive         recursive (default: on)\n");
    printf("  -i, --ignore-case       case-insensitive\n");
    printf("  -n, --line-number       show line numbers\n");
    printf("  -v, --invert            invert matches\n");
    printf("  -F, --fixed             literal match (no regex)\n");
    printf("  -l, --files-with        only show filenames\n");
    printf("  -I, --no-ignore         don't skip hidden/.git\n");
    printf("  --no-color              disable colors\n");
    printf("  --classic               GNU grep mode\n");
    printf("      --help              show this help\n");
    printf("      --version           show version\n");
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    color_enable();
    opts.recursive = 1;
    opts.show_color = 1;
    opts.show_line_num = 1;
    ignore_files = hash_set_new();
    ignore_dirs = hash_set_new();
    for (int i = 0; default_hidden[i]; i++) {
        hash_set_add(ignore_dirs, default_hidden[i]);
    }

    static const struct option longopts[] = {
        { "recursive",   no_argument,       NULL, 'r' },
        { "ignore-case", no_argument,       NULL, 'i' },
        { "line-number", no_argument,       NULL, 'n' },
        { "invert",      no_argument,       NULL, 'v' },
        { "fixed",       no_argument,       NULL, 'F' },
        { "files-with",  no_argument,       NULL, 'l' },
        { "no-ignore",   no_argument,       NULL, 'I' },
        { "no-color",    no_argument,       NULL, 1000 },
        { "classic",     no_argument,       NULL, 1001 },
        { "help",        no_argument,       NULL, 'h' },
        { "version",     no_argument,       NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "rinvlFIh", longopts, NULL)) != -1) {
        switch (opt) {
        case 'r': opts.recursive = 1; break;
        case 'i': opts.ignore_case = 1; break;
        case 'n': opts.show_line_num = 1; break;
        case 'v': opts.invert = 1; break;
        case 'F': opts.fixed = 1; break;
        case 'l': opts.only_files = 1; break;
        case 'I': opts.no_ignore = 1; break;
        case 1000: color_disable(); opts.show_color = 0; break;
        case 1001: opts.classic = 1; opts.show_color = 0; opts.recursive = 0; break;
        case 'h': usage(); break;
        case 'V': version(); break;
        default: return 2;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "%s: missing PATTERN\n", program_name);
        usage();
        return 2;
    }
    opts.pattern = xstrdup(argv[optind]);
    optind++;
    if (!opts.fixed) {
        int rc = regcomp(&opts.compiled, opts.pattern,
                          opts.ignore_case ? REG_ICASE : 0);
        if (rc != 0) {
            fprintf(stderr, "%s: invalid pattern\n", program_name);
            return 2;
        }
    }

    if (optind >= argc) {
        /* stdin */
        if (isatty(STDIN_FILENO) && !opts.classic) {
            /* 交互模式则警告；本骨架直接退出 */
        }
        opts.recursive = 0;
        if (!grep_file("(stdin)", stdin)) return 1;
    } else {
        for (int i = optind; i < argc; i++) {
            walk(argv[i]);
        }
    }
    return 0;
}
