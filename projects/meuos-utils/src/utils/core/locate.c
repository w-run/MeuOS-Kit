/* locate — 文件路径快速查找工具
 * 使用简单的文本数据库（每行一个路径）
 * 数据库默认位于 /var/lib/locatedb 或 ~/.cache/msh-locatedb
 *
 * 用法：
 *   locate [options] PATTERN
 *   locate -u [--database FILE]  更新数据库
 *   locate -i PATTERN            不区分大小写
 *   locate -r PATTERN            使用正则表达式
 *   locate -c PATTERN            只输出匹配数量
 *   locate -l N PATTERN          限制输出行数
 *   locate --database FILE       指定数据库
 *   locate --version / --help
 */

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

#include "meuos/utils.h"


static const char *default_db(void) {
    const char *home = getenv("HOME");
    if (home) {
        static char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/.cache/msh-locatedb", home);
        /* Check if home cache exists, otherwise try system db */
        struct stat st;
        if (stat(path, &st) == 0) return path;
    }
    return "/var/lib/locatedb";
}

/* === 数据库构建 === */
static int db_fd;
static long db_count;

static void walk_dir(const char *path, int max_depth) {
    if (max_depth <= 0) return;
    DIR *d = opendir(path);
    if (!d) return;
    
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        
        char fpath[PATH_MAX];
        snprintf(fpath, sizeof(fpath), "%s/%s", path, e->d_name);
        
        /* Skip VCS and cache dirs */
        if (strcmp(e->d_name, ".git") == 0 || strcmp(e->d_name, ".hg") == 0 ||
            strcmp(e->d_name, ".svn") == 0 || strcmp(e->d_name, "node_modules") == 0 ||
            strcmp(e->d_name, "__pycache__") == 0 || strcmp(e->d_name, ".cache") == 0) {
            continue;
        }
        
        struct stat st;
        if (lstat(fpath, &st) != 0) continue;
        
        /* Write path to database */
        int len = strlen(fpath);
        write(db_fd, fpath, len);
        write(db_fd, "\n", 1);
        db_count++;
        
        if (S_ISDIR(st.st_mode)) {
            walk_dir(fpath, max_depth - 1);
        }
    }
    closedir(d);
}

static int do_updatedb(const char *dbpath, const char *root) {
    db_fd = open(dbpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (db_fd < 0) die("cannot create %s: %s", dbpath, strerror(errno));
    
    db_count = 0;
    walk_dir(root ? root : ".", 20);
    
    close(db_fd);
    fprintf(stderr, "%s: database updated: %ld entries\n", program_name, db_count);
    return 0;
}

/* === 查询 === */
static int do_locate(const char *pattern, const char *dbpath,
                     int case_insensitive, int use_regex, int count_only,
                     int limit) {
    FILE *db = fopen(dbpath, "r");
    if (!db) {
        fprintf(stderr, "%s: cannot open database %s: %s\n", program_name, dbpath, strerror(errno));
        fprintf(stderr, "%s: hint: run '%s -u' to build the database\n", program_name, program_name);
        return 1;
    }
    
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    int match_count = 0;
    int printed = 0;
    regex_t re;
    int re_compiled = 0;
    
    if (use_regex) {
        int flags = REG_EXTENDED | REG_NOSUB;
        if (case_insensitive) flags |= REG_ICASE;
        if (regcomp(&re, pattern, flags) != 0) {
            die("invalid regex: %s", pattern);
        }
        re_compiled = 1;
    }
    
    while ((n = getline(&line, &cap, db)) > 0) {
        /* Strip newline */
        if (n > 0 && line[n-1] == '\n') line[--n] = '\0';
        
        int match = 0;
        if (use_regex) {
            match = (regexec(&re, line, 0, NULL, 0) == 0);
        } else if (case_insensitive) {
            /* Case-insensitive substring match */
            match = (strcasestr(line, pattern) != NULL);
        } else {
            match = (strstr(line, pattern) != NULL);
        }
        
        if (match) {
            match_count++;
            if (!count_only && (limit <= 0 || printed < limit)) {
                printf("%s\n", line);
                printed++;
            }
        }
    }
    
    if (re_compiled) regfree(&re);
    if (line) free(line);
    fclose(db);
    
    if (count_only) {
        printf("%d\n", match_count);
    }
    
    return match_count > 0 ? 0 : 1;
}

static void usage(void) {
    printf(
        "locate — find files by name (meuos-utils)\n"
        "\n"
        "usage: locate [options] PATTERN\n"
        "       locate -u [--database FILE] [--root DIR]\n"
        "\n"
        "options:\n"
        "  -u, --update       build/update the database\n"
        "  -i, --ignore-case  case-insensitive matching\n"
        "  -r, --regex        use extended regular expressions\n"
        "  -c, --count         only print count of matches\n"
        "  -l N, --limit N     limit output to N results\n"
        "  -d, --database FILE use FILE as database\n"
        "      --root DIR      root directory for -u (default: .)\n"
        "      --version       show version\n"
        "      --help          show this help\n"
        "\n"
        "database defaults to ~/.cache/msh-locatedb or /var/lib/locatedb\n");
}

int main(int argc, char **argv) {
    int do_update = 0;
    int case_insensitive = 0;
    int use_regex = 0;
    int count_only = 0;
    int limit = 0;
    const char *dbpath = NULL;
    const char *root = NULL;
    int oi = 1;
    
    while (oi < argc && argv[oi][0] == '-' && argv[oi][1] != '\0') {
        const char *opt = argv[oi];
        if (strcmp(opt, "--help") == 0) { usage(); return 0; }
        if (strcmp(opt, "--version") == 0) { printf("locate (meuos-utils)\n"); return 0; }
        if (strcmp(opt, "-u") == 0 || strcmp(opt, "--update") == 0) { do_update = 1; oi++; continue; }
        if (strcmp(opt, "-i") == 0 || strcmp(opt, "--ignore-case") == 0) { case_insensitive = 1; oi++; continue; }
        if (strcmp(opt, "-r") == 0 || strcmp(opt, "--regex") == 0) { use_regex = 1; oi++; continue; }
        if (strcmp(opt, "-c") == 0 || strcmp(opt, "--count") == 0) { count_only = 1; oi++; continue; }
        if (strcmp(opt, "-d") == 0 || strcmp(opt, "--database") == 0) {
            if (++oi >= argc) die("--database requires an argument");
            dbpath = argv[oi]; oi++; continue;
        }
        if (strncmp(opt, "--database=", 11) == 0) { dbpath = opt + 11; oi++; continue; }
        if (strcmp(opt, "-l") == 0 || strcmp(opt, "--limit") == 0) {
            if (++oi >= argc) die("--limit requires an argument");
            limit = atoi(argv[oi]); oi++; continue; }
        if (strncmp(opt, "-l", 2) == 0 && isdigit(opt[2])) { limit = atoi(opt + 2); oi++; continue; }
        if (strcmp(opt, "--root") == 0) {
            if (++oi >= argc) die("--root requires an argument");
            root = argv[oi]; oi++; continue; }
        if (opt[0] == '-' && opt[1] == '-' && opt[2] == 'd' && opt[3] == '=') {
            dbpath = opt + 4; oi++; continue; }
        if (opt[0] == '-' && opt[1] == '-' && opt[2] == 'r' && opt[3] == 'o') {
            /* --root=DIR */
            root = strchr(opt, '=') + 1; oi++; continue; }
        die("unknown option: %s", opt);
    }
    
    if (!dbpath) dbpath = default_db();
    
    if (do_update) {
        return do_updatedb(dbpath, root);
    }
    
    if (oi >= argc) {
        fprintf(stderr, "%s: no pattern specified (try --help)\n", program_name);
        return 2;
    }
    
    return do_locate(argv[oi], dbpath, case_insensitive, use_regex, count_only, limit);
}
