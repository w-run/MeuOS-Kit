/* ls — 现代列表（MeuOS Next 版）
 *
 * 默认行为：
 *   - 不显示隐藏文件（点开头）
 *   - 不递归
 *   - 单列输出（图标 + 名称）
 *   - 彩色 + 文件类型图标
 *   - 按名称排序，目录优先
 *
 * GNU 兼容模式：--classic 切换为 POSIX/BSD 风格。
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "meuos/color.h"
#include "meuos/icons.h"
#include "meuos/utils.h"

typedef struct {
    char *name;           /* basename */
    char *fullpath;
    mode_t mode;
    uid_t uid;
    gid_t gid;
    off_t size;
    time_t mtime;
    nlink_t nlink;
    int is_link;
    char *link_target;    /* for symlinks */
} fileinfo_t;

typedef enum {
    SORT_NAME,
    SORT_SIZE,
    SORT_TIME,
    SORT_NONE,
} sort_mode_t;

static struct {
    int show_all;         /* 包括 . 和 .. */
    int almost_all;       /* 包括 . 开头的但不包括 . 和 .. */
    int long_fmt;
    int recursive;
    int classic;          /* --classic: GNU 风格 */
    int json_out;
    int tree_view;
    int tree_depth;
    int reverse_sort;
    int show_icons;
    int show_color;
    sort_mode_t sort;
} opts;

static const char *mode_string(mode_t m) {
    static char buf[11];
    buf[0] = (S_ISDIR(m)) ? 'd' :
             (S_ISLNK(m)) ? 'l' :
             (S_ISFIFO(m)) ? 'p' :
             (S_ISSOCK(m)) ? 's' :
             (S_ISBLK(m)) ? 'b' :
             (S_ISCHR(m)) ? 'c' : '-';
    buf[1] = (m & S_IRUSR) ? 'r' : '-';
    buf[2] = (m & S_IWUSR) ? 'w' : '-';
    buf[3] = (m & S_IXUSR) ? 'x' : '-';
    buf[4] = (m & S_IRGRP) ? 'r' : '-';
    buf[5] = (m & S_IWGRP) ? 'w' : '-';
    buf[6] = (m & S_IXGRP) ? 'x' : '-';
    buf[7] = (m & S_IROTH) ? 'r' : '-';
    buf[8] = (m & S_IWOTH) ? 'w' : '-';
    buf[9] = (m & S_IXOTH) ? 'x' : '-';
    buf[10] = '\0';
    return buf;
}

static char *human_size(off_t n) {
    static char buf[16];
    if (n < 0) {
        snprintf(buf, sizeof(buf), "?");
    } else if (n < 1024) {
        snprintf(buf, sizeof(buf), "%lld", (long long)n);
    } else {
        char units[] = "KMGT";
        double v = n;
        int i = 0;
        while (v >= 1024.0 && i < 4) { v /= 1024.0; i++; }
        snprintf(buf, sizeof(buf), "%.1f%c", v, units[i]);
    }
    return buf;
}

static int name_cmp(const void *a, const void *b) {
    const fileinfo_t *fa = a, *fb = b;
    /* 目录优先 */
    int da = S_ISDIR(fa->mode), db = S_ISDIR(fb->mode);
    if (da != db) return db - da;
    return strcmp(fa->name, fb->name);
}

static int size_cmp(const void *a, const void *b) {
    const fileinfo_t *fa = a, *fb = b;
    int da = S_ISDIR(fa->mode), db = S_ISDIR(fb->mode);
    if (da != db) return db - da;
    if (fa->size < fb->size) return -1;
    if (fa->size > fb->size) return 1;
    return 0;
}

static int time_cmp(const void *a, const void *b) {
    const fileinfo_t *fa = a, *fb = b;
    int da = S_ISDIR(fa->mode), db = S_ISDIR(fb->mode);
    if (da != db) return db - da;
    if (fa->mtime < fb->mtime) return 1;
    if (fa->mtime > fb->mtime) return -1;
    return 0;
}

static int collect(const char *dir, fileinfo_t **out, size_t *n_out) {
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "%s: cannot access '%s': %s\n",
                program_name, dir, strerror(errno));
        return -1;
    }
    size_t cap = 32;
    size_t n = 0;
    fileinfo_t *arr = xmalloc(cap * sizeof(fileinfo_t));
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!opts.show_all && !opts.almost_all && de->d_name[0] == '.') continue;
        if ((opts.almost_all || !opts.show_all) && 
            (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)) continue;
        if (n >= cap) { cap *= 2; arr = xrealloc(arr, cap * sizeof(*arr)); }
        char fullpath[PATH_MAX];
        if (strcmp(dir, ".") == 0) {
            snprintf(fullpath, sizeof(fullpath), "%s", de->d_name);
        } else {
            snprintf(fullpath, sizeof(fullpath), "%s/%s", dir, de->d_name);
        }
        struct stat st;
        int is_link = (de->d_type == DT_LNK);
        if (lstat(fullpath, &st) < 0) continue;
        arr[n].name = xstrdup(de->d_name);
        arr[n].fullpath = xstrdup(fullpath);
        arr[n].mode = st.st_mode;
        arr[n].uid = st.st_uid;
        arr[n].gid = st.st_gid;
        arr[n].size = st.st_size;
        arr[n].mtime = st.st_mtime;
        arr[n].nlink = st.st_nlink;
        arr[n].is_link = is_link;
        arr[n].link_target = NULL;
        if (is_link) {
            char linkbuf[PATH_MAX];
            ssize_t nread = readlink(fullpath, linkbuf, sizeof(linkbuf) - 1);
            if (nread >= 0) { linkbuf[nread] = '\0'; arr[n].link_target = xstrdup(linkbuf); }
        }
        n++;
    }
    closedir(d);

    /* 排序 */
    int (*cmp)(const void *, const void *) = name_cmp;
    switch (opts.sort) {
    case SORT_SIZE: cmp = size_cmp; break;
    case SORT_TIME: cmp = time_cmp; break;
    case SORT_NAME: cmp = name_cmp; break;
    default: break;
    }
    qsort(arr, n, sizeof(fileinfo_t), cmp);
    if (opts.reverse_sort) {
        for (size_t i = 0; i < n / 2; i++) {
            fileinfo_t tmp = arr[i];
            arr[i] = arr[n - 1 - i];
            arr[n - 1 - i] = tmp;
        }
    }
    *out = arr;
    *n_out = n;
    return 0;
}

static void print_long(fileinfo_t *f) {
    struct passwd *pw = getpwuid(f->uid);
    struct group *gr = getgrgid(f->gid);
    char timebuf[64];
    struct tm *tm = localtime(&f->mtime);
    strftime(timebuf, sizeof(timebuf), "%b %d %H:%M", tm);

    if (opts.show_icons && opts.show_color) {
        fputs(icon_for_mode(f->mode, f->is_link), stdout);
        fputs(" ", stdout);
    }
    if (opts.show_color) {
        fputs(color_for_mode(f->mode), stdout);
    }
    printf("%s %2lu %-8s %-8s %8s %s %s",
           mode_string(f->mode),
           (unsigned long)f->nlink,
           pw ? pw->pw_name : "-",
           gr ? gr->gr_name : "-",
           human_size(f->size),
           timebuf,
           f->name);
    if (opts.show_color) {
        fputs(color_reset(), stdout);
    }
    if (f->is_link && f->link_target) {
        printf(" -> %s", f->link_target);
    }
    fputc('\n', stdout);
}

static void print_short(fileinfo_t *f) {
    /* 现代模式：图标作为前缀（Unicode/Nerd），或后缀（ASCII ls -F 风格） */
    icon_set_t iset = icon_set(ICON_SET_ASCII); /* 获取当前 */
    icon_set(iset); /* 恢复（上面调用有副作用） */
    if (opts.show_icons && iset >= ICON_SET_UNICODE) {
        /* Unicode/Nerd: 图标在前 */
        fputs(icon_for_mode(f->mode, f->is_link), stdout);
        fputs(" ", stdout);
    }
    if (opts.show_color) {
        fputs(color_for_mode(f->mode), stdout);
    }
    printf("%s", f->name);
    /* ASCII 模式: 后缀图标（ls -F 风格） */
    if (opts.show_icons && iset == ICON_SET_ASCII) {
        const char *suffix = icon_for_mode(f->mode, f->is_link);
        if (*suffix) fputs(suffix, stdout);
    }
    if (f->is_link && f->link_target) {
        printf(" → %s", f->link_target);
    }
    if (opts.show_color) {
        fputs(color_reset(), stdout);
    }
    fputc('\n', stdout);
}

static void print_json(fileinfo_t *f, int first) {
    if (!first) fputs(",\n", stdout);
    fputs("  {\"name\":\"", stdout);
    for (const char *p = f->name; *p; p++) {
        if (*p == '"' || *p == '\\') fputc('\\', stdout);
        if (*p >= 32) fputc(*p, stdout);
    }
    fputs("\",\"type\":\"", stdout);
    if (S_ISDIR(f->mode)) fputs("dir", stdout);
    else if (S_ISLNK(f->mode)) fputs("link", stdout);
    else fputs("file", stdout);
    printf("\",\"size\":%lld,\"mode\":\"%s\"}",
           (long long)f->size, mode_string(f->mode));
}

static void list_directory(const char *path, int depth, int is_tree) {
    fileinfo_t *arr; size_t n;
    if (collect(path, &arr, &n) < 0) return;

    /* header */
    if (is_tree) {
        if (depth > 0) {
            for (int i = 0; i < depth - 1; i++) printf("│   ");
        }
    } else if (opts.recursive) {
        if (depth == 0) {
            printf("\033[1m%s\033[0m:\n", path);
        } else {
            printf("\n\033[1m%s\033[0m:\n", path);
        }
    } else if (depth == 0) {
        /* 默认 header（现代模式不显示） */
    }

    /* 输出 */
    for (size_t i = 0; i < n; i++) {
        if (is_tree && depth > 0) {
            printf("├── ");
        }
        if (opts.json_out && depth == 0) {
            print_json(&arr[i], i == 0);
        } else if (opts.long_fmt) {
            print_long(&arr[i]);
        } else {
            print_short(&arr[i]);
        }
    }

    /* JSON 收尾 */
    if (opts.json_out && depth == 0) {
        fputs("\n]\n", stdout);
    }

    /* 递归 */
    if (opts.recursive || (is_tree && (opts.tree_depth < 0 || depth < opts.tree_depth))) {
        for (size_t i = 0; i < n; i++) {
            if (S_ISDIR(arr[i].mode) && strcmp(arr[i].name, ".") != 0
                && strcmp(arr[i].name, "..") != 0) {
                list_directory(arr[i].fullpath, depth + 1, is_tree);
            }
        }
    }

    for (size_t i = 0; i < n; i++) {
        free(arr[i].name);
        free(arr[i].fullpath);
        free(arr[i].link_target);
    }
    free(arr);
}

static void list_single_file(const char *path) {
    struct stat st;
    if (lstat(path, &st) < 0) {
        fprintf(stderr, "%s: cannot access '%s': %s\n",
                program_name, path, strerror(errno));
        return;
    }
    fileinfo_t f = {0};
    f.name = (char *)path;
    f.fullpath = (char *)path;
    f.mode = st.st_mode;
    f.uid = st.st_uid;
    f.gid = st.st_gid;
    f.size = st.st_size;
    f.mtime = st.st_mtime;
    f.nlink = st.st_nlink;
    f.is_link = S_ISLNK(st.st_mode);
    if (opts.long_fmt) print_long(&f);
    else print_short(&f);
}

static void usage(void) {
    printf("Usage: %s [OPTIONS] [PATH...]\n", program_name);
    printf("\n");
    printf("List directory contents in the modern MeuOS style.\n\n");
    printf("  -a, --all                  include hidden files (starting with .)\n");
    printf("  -l, --long                 long format\n");
    printf("  -t, --sort=time            sort by modification time (newest first)\n");
    printf("  -S, --sort=size            sort by file size (largest first)\n");
    printf("  -r, --reverse              reverse sort order\n");
    printf("  -R, --recursive            list subdirectories recursively\n");
    printf("  --tree [DEPTH]             show tree view (default 3 levels)\n");
    printf("  --json                     output as JSON\n");
    printf("  --no-icons                 disable file type icons\n");
    printf("  --no-color                 disable colors\n");
    printf("  --classic                  GNU-compatible mode (POSIX style)\n");
    printf("      --help                 show this help\n");
    printf("      --version              show version\n");
    printf("\n");
    printf("Default mode is modern: icons, colors, human-readable, single column.\n");
    printf("Use --classic for traditional output (for scripts).\n");
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    color_enable();

    static const struct option longopts[] = {
        { "all",       no_argument, NULL, 'a' },
        { "long",      no_argument, NULL, 'l' },
        { "sort",      required_argument, NULL, 1000 },
        { "reverse",   no_argument, NULL, 'r' },
        { "recursive", no_argument, NULL, 'R' },
        { "tree",      optional_argument, NULL, 1001 },
        { "json",      no_argument, NULL, 1002 },
        { "no-icons",  no_argument, NULL, 1003 },
        { "no-color",  no_argument, NULL, 1004 },
        { "classic",   no_argument, NULL, 1005 },
        { "help",      no_argument, NULL, 'h' },
        { "version",   no_argument, NULL, 'V' },
        { NULL,        0, NULL,  0 },
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "alltrSRh", longopts, NULL)) != -1) {
        switch (opt) {
        case 'a': opts.show_all = 1; break;
        case 'l': opts.long_fmt = 1; break;
        case 't': opts.sort = SORT_TIME; break;
        case 'r': opts.reverse_sort = 1; break;
        case 'S': opts.sort = SORT_SIZE; break;
        case 'R': opts.recursive = 1; break;
        case 1000: /* --sort */
            if (strcmp(optarg, "time") == 0) opts.sort = SORT_TIME;
            else if (strcmp(optarg, "size") == 0) opts.sort = SORT_SIZE;
            else if (strcmp(optarg, "name") == 0) opts.sort = SORT_NAME;
            else if (strcmp(optarg, "none") == 0) opts.sort = SORT_NONE;
            else { fprintf(stderr, "%s: invalid --sort '%s'\n", program_name, optarg); return 2; }
            break;
        case 1001: /* --tree */
            opts.tree_view = 1;
            opts.tree_depth = optarg ? atoi(optarg) : 3;
            break;
        case 1002: opts.json_out = 1; break;
        case 1003: opts.show_icons = 0; break;
        case 1004: color_disable(); opts.show_color = 0; break;
        case 1005: opts.classic = 1; opts.show_icons = 0; color_disable(); opts.show_color = 0; break;
        case 'h': usage(); break;
        case 'V': version(); break;
        default: return 2;
        }
    }

    /* 默认设置（除非 --classic） */
    if (!opts.classic) {
        opts.almost_all = 1;  /* 类似 .hidden 但不显示 . 和 .. */
        opts.show_color = 1;
        color_enable();
        opts.show_icons = 1;
        /* 自动检测最佳图标集 */
        icon_set(icon_auto_detect());
    } else {
        opts.show_icons = 0;
        color_disable();
        opts.show_color = 0;
    }

    /* 路径 */
    int npaths = argc - optind;
    if (npaths == 0) {
        if (opts.json_out) fputs("[\n", stdout);
        list_directory(".", 0, opts.tree_view);
        if (opts.json_out) fputs("]\n", stdout);
    } else if (npaths == 1) {
        const char *p = argv[optind];
        struct stat st;
        if (lstat(p, &st) == 0 && !S_ISDIR(st.st_mode)) {
            list_single_file(p);
        } else {
            if (opts.json_out) fputs("[\n", stdout);
            list_directory(p, 0, opts.tree_view);
            if (opts.json_out) fputs("]\n", stdout);
        }
    } else {
        for (int i = optind; i < argc; i++) {
            printf("\033[1m%s\033[0m:\n", argv[i]);
            struct stat st;
            if (lstat(argv[i], &st) == 0 && !S_ISDIR(st.st_mode)) {
                list_single_file(argv[i]);
            } else {
                list_directory(argv[i], 0, opts.tree_view);
            }
            if (i + 1 < argc) fputc('\n', stdout);
        }
    }

    return 0;
}
