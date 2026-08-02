/* find - fd 风格现代 find（MeuOS Next 版）
 *
 * 默认（现代模式）：
 *   - regex 模式匹配 basename
 *   - 自动递归
 *   - 跳过 .git、.gitignore 覆盖目录
 *   - 彩色高亮匹配
 *
 * --classic: GNU find 风格 POSIX 谓词子集
 *   [-name PAT] [-type [fdl]] [-maxdepth N] [-print]
 *   [-exec CMD {} ;] [-delete]
 *
 * 谓词组合：隐式 AND（今日不做 -or/! ）
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <getopt.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "meuos/color.h"
#include "meuos/hash.h"
#include "meuos/utils.h"

/* === 共享选项 === */
static struct {
    int classic;
    int hidden;          /* --hidden: 包括 . 开头的 */
    int no_ignore;       /* --no-ignore: 不读 .gitignore */
    int case_sensitive;
    int follow_symlinks;
    int max_depth;
    int show_color;
    int full_path;       /* 现代模式：匹配完整路径而非 basename */
    char *match_pattern;  /* 现代模式：regex 主搜索 pattern */
    regex_t compiled;
    int has_pattern;
} opts;

/* === classic 谓词 === */
typedef struct {
    char *name_pat;       /* -name PATTERN（fnmatch） */
    char type_filter;     /* -type X：'f'/'d'/'l'/0 */
    int  has_maxdepth;    /* -maxdepth N（与 opts.max_depth 同义） */
    int  do_print;        /* -print 显式 */
    int  do_delete;       /* -delete */
    char **exec_argv;     /* -exec CMD ... ; */
    int    exec_argc;
    int    has_exec;
} predicates_t;

static predicates_t preds;

/* === 默认忽略的目录名 === */
static const char *default_hidden[] = {
    ".git", ".hg", ".svn", "node_modules",
    "target", "build", "dist", "__pycache__",
    ".cache", ".venv", "venv",
    NULL
};

static hash_set_t *ignore_dirs;

static int should_ignore(const char *name) {
    if (!opts.hidden && name[0] == '.') {
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
        return 1;
    }
    if (hash_set_has(ignore_dirs, name)) return 1;
    return 0;
}

/* 简化的 .gitignore 加载：字面目录名入 hash_set；glob 模式存独立数组。
 * 今日不做完整 gitignore 语义，仅做最常见的字面目录排除。 */
static char **glob_ignores;
static size_t glob_ignores_n;
static size_t glob_ignores_cap;

static void load_gitignore(const char *dir) {
    if (opts.no_ignore) return;
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/.gitignore", dir);
    FILE *fp = fopen(path, "r");
    if (!fp) return;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '\n' || *s == '#' || *s == '\0') continue;
        char *end = s + strlen(s);
        while (end > s && (end[-1] == '\n' || end[-1] == ' ' || end[-1] == '\t')) {
            end--;
        }
        *end = '\0';
        if (*s == '!') continue;            /* 反向模式：跳过 */
        char *slash = strchr(s, '/');
        if (slash) *slash = '\0';            /* 仅取首段 */
        if (strchr(s, '*') || strchr(s, '?') || strchr(s, '[')) {
            /* glob 模式：存数组 */
            if (glob_ignores_n == glob_ignores_cap) {
                glob_ignores_cap = glob_ignores_cap ? glob_ignores_cap * 2 : 8;
                glob_ignores = xrealloc(glob_ignores, glob_ignores_cap * sizeof(char *));
            }
            glob_ignores[glob_ignores_n++] = xstrdup(s);
        } else if (*s) {
            hash_set_add(ignore_dirs, s);
        }
    }
    fclose(fp);
}

static int name_ignored(const char *name) {
    for (size_t i = 0; i < glob_ignores_n; i++) {
        if (fnmatch(glob_ignores[i], name, 0) == 0) return 1;
    }
    return 0;
}

/* === 输出 === */
static void print_match(const char *path) {
    if (!opts.match_pattern || opts.classic) {
        /* classic 模式或无 pattern：朴素输出 */
        if (opts.show_color && color_enabled && !opts.classic) {
            fputs(MEUOS_THEME_ACCENT, stdout);
            puts(path);
            fputs(color_reset(), stdout);
        } else {
            puts(path);
        }
        return;
    }
    if (!opts.show_color || !color_enabled) { puts(path); return; }
    /* 现代模式：高亮匹配的子串 */
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
        fwrite(path + offset, 1, (size_t)(start - offset), stdout);
        fputs(COLOR_YELLOW_FG, stdout);
        fwrite(path + start, 1, (size_t)(end - start), stdout);
        fputs(color_reset(), stdout);
        fputs(color_for_mode(S_IFREG), stdout);
        offset = end;
        p = path + offset;
    }
    fwrite(path + (size_t)offset, 1, (size_t)(len - offset), stdout);
    fputs(color_reset(), stdout);
    fputc('\n', stdout);
}

/* === classic 谓词评估 === */
static int type_matches(char type, mode_t m) {
    switch (type) {
    case 'f': return S_ISREG(m);
    case 'd': return S_ISDIR(m);
    case 'l': return S_ISLNK(m);
    case 0:   return 1;
    default:  return 0;
    }
}

static int preds_match(const char *path, const char *basename, mode_t m) {
    (void)path;  /* classic -name 仅匹配 basename，与 GNU find 一致 */
    if (preds.name_pat) {
        if (fnmatch(preds.name_pat, basename, 0) != 0) return 0;
    }
    if (preds.type_filter && !type_matches(preds.type_filter, m)) return 0;
    return 1;
}

/* 执行 -exec CMD {} ; */
static int run_exec(const char *path) {
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }
    if (pid == 0) {
        /* 子进程：组装 argv，把 {} 替换为 path */
        char **av = xmalloc((size_t)(preds.exec_argc + 1) * sizeof(char *));
        for (int i = 0; i < preds.exec_argc; i++) {
            if (strcmp(preds.exec_argv[i], "{}") == 0) {
                av[i] = xstrdup(path);
            } else {
                av[i] = preds.exec_argv[i];
            }
        }
        av[preds.exec_argc] = NULL;
        execvp(av[0], av);
        fprintf(stderr, "%s: %s: %s\n", program_name, av[0], strerror(errno));
        _exit(127);
    }
    int status;
    if (waitpid(pid, &status, 0) < 0) { perror("waitpid"); return -1; }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

static void act_on_match(const char *path) {
    if (preds.has_exec) {
        run_exec(path);
    }
    if (preds.do_delete) {
        if (unlink(path) < 0 && rmdir(path) < 0) {
            fprintf(stderr, "%s: cannot delete '%s': %s\n",
                    program_name, path, strerror(errno));
        }
    }
    /* 默认动作或显式 -print：输出路径 */
    if (preds.do_print || (!preds.has_exec && !preds.do_delete)) {
        print_match(path);
    }
}

/* === 主 walk === */
static void walk(const char *path, int depth) {
    DIR *d;
    struct stat st_top;
    if (opts.follow_symlinks) {
        d = opendir(path);
    } else {
        if (lstat(path, &st_top) < 0) return;
        if (!S_ISDIR(st_top.st_mode)) {
            /* 单文件 */
            int matched = 1;
            if (opts.classic) {
                const char *base = strrchr(path, '/');
                base = base ? base + 1 : path;
                matched = preds_match(path, base, st_top.st_mode);
            } else if (opts.has_pattern) {
                matched = (regexec(&opts.compiled, path, 0, NULL, 0) == 0);
            }
            if (matched) {
                if (opts.classic) {
                    act_on_match(path);
                } else {
                    print_match(path);
                }
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
        if (name_ignored(de->d_name)) continue;

        /* maxdepth: 子项层级 = depth + 1，超过则跳过（不处理、不递归） */
        int child_level = depth + 1;
        if (opts.max_depth >= 0 && child_level > opts.max_depth) continue;

        char full[PATH_MAX];
        if (strcmp(path, ".") == 0) {
            snprintf(full, sizeof(full), "%s", de->d_name);
        } else {
            snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
        }

        struct stat st;
        if (lstat(full, &st) < 0) continue;

        int matched = 1;
        if (opts.classic) {
            matched = preds_match(full, de->d_name, st.st_mode);
        } else if (opts.has_pattern) {
            if (opts.full_path) {
                matched = (regexec(&opts.compiled, full, 0, NULL, 0) == 0);
            } else {
                matched = (regexec(&opts.compiled, de->d_name, 0, NULL, 0) == 0);
            }
        }

        if (matched) {
            if (opts.classic) {
                act_on_match(full);
            } else {
                print_match(full);
            }
        }

        if (S_ISDIR(st.st_mode)) {
            walk(full, depth + 1);
        }
    }
    closedir(d);
}

/* === classic 谓词解析 === */
/* argv[start..argc) 形如：[PATH] [-name PAT] [-type X] [-maxdepth N]
 *                          [-print] [-exec CMD {} ;] [-delete] [PATH]
 * PATH 是第一个非 '-' 开头的 token（可在谓词前或后），默认 "."。
 * 谓词以隐式 AND 组合（不做 -and/-or/!）。
 * 返回值：成功 -> argv 中 PATH 的 index（若无 PATH 返回 argc）；
 *        失败 -> -1。 */
static int parse_predicates(int argc, char **argv, int start) {
    int i = start;
    int path_idx = -1;
    while (i < argc) {
        const char *a = argv[i];
        if (strcmp(a, "-name") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: -name requires argument\n", program_name);
                return -1;
            }
            free(preds.name_pat);
            preds.name_pat = xstrdup(argv[i + 1]);
            i += 2;
        } else if (strcmp(a, "-type") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: -type requires argument\n", program_name);
                return -1;
            }
            char t = argv[i + 1][0];
            if (t != 'f' && t != 'd' && t != 'l') {
                fprintf(stderr, "%s: -type: unknown type '%c'\n",
                        program_name, t);
                return -1;
            }
            preds.type_filter = t;
            i += 2;
        } else if (strcmp(a, "-maxdepth") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: -maxdepth requires argument\n",
                        program_name);
                return -1;
            }
            char *end = NULL;
            long n = strtol(argv[i + 1], &end, 10);
            if (end == argv[i + 1] || *end != '\0' || n < 0) {
                fprintf(stderr, "%s: -maxdepth: invalid number '%s'\n",
                        program_name, argv[i + 1]);
                return -1;
            }
            opts.max_depth = (int)n;
            preds.has_maxdepth = 1;
            i += 2;
        } else if (strcmp(a, "-print") == 0) {
            preds.do_print = 1;
            i += 1;
        } else if (strcmp(a, "-delete") == 0) {
            preds.do_delete = 1;
            i += 1;
        } else if (strcmp(a, "-exec") == 0) {
            /* 收集到 ; 或 + */
            int j = i + 1;
            int semicolon = -1;
            for (; j < argc; j++) {
                if (strcmp(argv[j], ";") == 0 || strcmp(argv[j], "+") == 0) {
                    semicolon = j;
                    break;
                }
            }
            if (semicolon < 0) {
                fprintf(stderr, "%s: -exec: missing terminator ';'\n",
                        program_name);
                return -1;
            }
            int n_args = semicolon - (i + 1);
            if (n_args < 1) {
                fprintf(stderr, "%s: -exec: empty command\n", program_name);
                return -1;
            }
            /* +1 for NULL terminator */
            preds.exec_argv = xmalloc((size_t)(n_args + 1) * sizeof(char *));
            preds.exec_argc = 0;
            for (int k = i + 1; k < semicolon; k++) {
                preds.exec_argv[preds.exec_argc++] = argv[k];
            }
            preds.exec_argv[preds.exec_argc] = NULL;
            preds.has_exec = 1;
            i = semicolon + 1;
        } else if (a[0] == '-' && a[1] != '\0') {
            /* 未识别的 -X 谓词 */
            fprintf(stderr, "%s: unknown predicate '%s'\n", program_name, a);
            return -1;
        } else {
            /* PATH：第一个非 '-' token */
            if (path_idx < 0) {
                path_idx = i;
            } else {
                fprintf(stderr, "%s: multiple paths not supported: '%s'\n",
                        program_name, a);
                return -1;
            }
            i += 1;
        }
    }
    return path_idx < 0 ? argc : path_idx;
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
    printf("      --full-path         match full path instead of basename\n");
    printf("      --no-color          disable colors\n");
    printf("      --classic           GNU find mode (POSIX -name, -type, ...)\n");
    printf("      --help              show this help\n");
    printf("      --version           show version\n");
    printf("\n");
    printf("Classic predicates (--classic):\n");
    printf("  -name PATTERN           basename fnmatch glob\n");
    printf("  -type [fdl]            file / directory / symlink\n");
    printf("  -maxdepth N            max recursion depth\n");
    printf("  -print                 default action\n");
    printf("  -exec CMD {} ;         execute command per match\n");
    printf("  -delete                delete matches\n");
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

    /* 先扫一遍 argv 找 modern 模式的长选项 + --classic
     * classic 模式下不调用 getopt，避免 -name/-type 等被误判为短选项 */
    int i = 1;
    const char *path = ".";
    while (i < argc) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0) { i++; break; }
        if (strcmp(a, "--classic") == 0) {
            opts.classic = 1;
            opts.show_color = 0;
            i++;
            continue;
        }
        if (strcmp(a, "--no-color") == 0) {
            color_disable();
            opts.show_color = 0;
            i++;
            continue;
        }
        if (strcmp(a, "--hidden") == 0 || strcmp(a, "-H") == 0) {
            opts.hidden = 1; i++; continue;
        }
        if (strcmp(a, "--no-ignore") == 0 || strcmp(a, "-I") == 0) {
            opts.no_ignore = 1; i++; continue;
        }
        if (strcmp(a, "--follow") == 0 || strcmp(a, "-L") == 0) {
            opts.follow_symlinks = 1; i++; continue;
        }
        if (strcmp(a, "--case-sensitive") == 0 || strcmp(a, "-s") == 0) {
            opts.case_sensitive = 1; i++; continue;
        }
        if (strcmp(a, "--full-path") == 0) {
            opts.full_path = 1; i++; continue;
        }
        if (strcmp(a, "--max-depth") == 0 || strcmp(a, "-d") == 0) {
            if (i + 1 < argc) {
                opts.max_depth = atoi(argv[i + 1]);
                preds.has_maxdepth = 1;
                i += 2;
                continue;
            }
            fprintf(stderr, "%s: --max-depth requires argument\n", program_name);
            return 2;
        }
        if (strncmp(a, "--max-depth=", 12) == 0) {
            opts.max_depth = atoi(a + 12);
            preds.has_maxdepth = 1;
            i++; continue;
        }
        if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
            usage(); return 0;
        }
        if (strcmp(a, "--version") == 0 || strcmp(a, "-V") == 0) {
            version();
        }
        break;  /* 第一个非选项参数，退出解析 */
    }

    if (opts.classic) {
        /* classic：解析谓词 + 路径 */
        int idx = parse_predicates(argc, argv, i);
        if (idx < 0) return 2;
        if (idx < argc && argv[idx][0] != '-') {
            path = argv[idx];
        }
        walk(path, 0);
        return 0;
    }

    /* 现代模式：[PATTERN] [PATH...] */
    if (i < argc) {
        const char *first = argv[i];
        if (strchr(first, '/') && argc - i > 1) {
            path = first;
        } else if (argc - i == 1 && strchr(first, '/')) {
            path = first;
        } else {
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
            if (i + 1 < argc) path = argv[i + 1];
        }
    }

    walk(path, 0);
    return 0;
}
