/* stat - 显示文件状态
 *
 * 默认（现代模式）：表格输出，包含 Size/Mode/Owner/Modify/Type 等
 * --classic：等价 `stat -c '%n %s %F %U %y'`，单行文本
 * --json：JSON 输出
 *
 * 不实现：文件系统 stat -f、复杂的 format 字符串（仅支持预定义格式）。
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <errno.h>
#include <getopt.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "meuos/utils.h"

static const char *file_type(mode_t m) {
    if (S_ISREG(m))  return "regular file";
    if (S_ISDIR(m))  return "directory";
    if (S_ISLNK(m))  return "symbolic link";
    if (S_ISCHR(m))  return "character device";
    if (S_ISBLK(m))  return "block device";
    if (S_ISFIFO(m)) return "fifo";
    if (S_ISSOCK(m)) return "socket";
    return "unknown";
}

static void mode_to_str(mode_t m, char *buf, size_t sz) {
    char t = '-';
    if (S_ISDIR(m))  t = 'd';
    else if (S_ISLNK(m)) t = 'l';
    else if (S_ISCHR(m)) t = 'c';
    else if (S_ISBLK(m)) t = 'b';
    else if (S_ISFIFO(m)) t = 'p';
    else if (S_ISSOCK(m)) t = 's';
    snprintf(buf, sz, "%c%c%c%c%c%c%c%c%c",
        t,
        (m & S_IRUSR) ? 'r' : '-',
        (m & S_IWUSR) ? 'w' : '-',
        (m & S_ISUID) ? 's' : ((m & S_IXUSR) ? 'x' : '-'),
        (m & S_IRGRP) ? 'r' : '-',
        (m & S_IWGRP) ? 'w' : '-',
        (m & S_ISGID) ? 's' : ((m & S_IXGRP) ? 'x' : '-'),
        (m & S_IROTH) ? 'r' : '-',
        (m & S_IWOTH) ? 'w' : '-');
}

static void format_time(time_t t, char *buf, size_t sz) {
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, sz, "%Y-%m-%d %H:%M:%S %z", &tm);
}

static int stat_one(const char *path, int classic, int json, int first) {
    struct stat st;
    if (lstat(path, &st) < 0) {
        fprintf(stderr, "%s: cannot stat '%s': %s\n",
                program_name, path, strerror(errno));
        return 1;
    }

    char mode_str[16];
    mode_to_str(st.st_mode, mode_str, sizeof(mode_str));
    char mtime[64], atime[64];
    format_time(st.st_mtime, mtime, sizeof(mtime));
    format_time(st.st_atime, atime, sizeof(atime));
    struct passwd *pw = getpwuid(st.st_uid);
    struct group *gr = getgrgid(st.st_gid);
    const char *owner = pw ? pw->pw_name : "?";
    const char *group = gr ? gr->gr_name : "?";

    if (json) {
        if (!first) printf(",\n");
        printf("  {\n");
        printf("    \"name\": \"%s\",\n", path);
        printf("    \"size\": %lld,\n", (long long)st.st_size);
        printf("    \"type\": \"%s\",\n", file_type(st.st_mode));
        printf("    \"mode\": \"%s\",\n", mode_str);
        printf("    \"owner\": \"%s\",\n", owner);
        printf("    \"group\": \"%s\",\n", group);
        printf("    \"modify\": \"%s\"\n", mtime);
        printf("  }");
        return 0;
    }

    if (classic) {
        printf("%s %lld %s %s %s\n", path, (long long)st.st_size,
               file_type(st.st_mode), owner, mtime);
    } else {
        /* 现代表格输出 */
        printf("  File: %s\n", path);
        printf("  Size: %-10lld\tType: %s\n", (long long)st.st_size,
               file_type(st.st_mode));
        printf("  Mode: %s\tUid: %s\tGid: %s\n", mode_str, owner, group);
        printf("  Modify: %s\n", mtime);
        printf("  Access: %s\n", atime);
    }
    return 0;
}

static void usage(void) {
    fprintf(stdout,
        "Usage: %s [OPTION]... FILE...\n"
        "Display file or file system status.\n\n"
        "  -c FORMAT   use FORMAT (only classic formats supported)\n"
        "      --classic  POSIX style (single line)\n"
        "      --json     JSON output\n"
        "      --help     display this help and exit\n"
        "      --version  output version information and exit\n",
        program_name);
    exit(0);
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    utils_classic_init(argc, argv);

    int classic = utils_classic_mode;
    int json = 0;

    static const struct utils_option longopts[] = {
        { "classic", no_argument, NULL, 1000 },
        { "json",    no_argument, NULL, 1001 },
        { "help",    no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };

    int opt;
    while ((opt = utils_getopt_long(argc, argv, "c:hV", longopts, NULL)) != -1) {
        switch (opt) {
        case 1000: classic = 1; break;
        case 1001: json = 1; break;
        case 'c': classic = 1; break;  /* 简化：-c FORMAT 视作 classic */
        case 'h': usage(); return 0;
        case 'V': version(); break;
        default:
            fprintf(stderr, "%s: try --help for more information\n", program_name);
            return 2;
        }
    }

    if (utils_optind >= argc) {
        fprintf(stderr, "%s: missing operand\n", program_name);
        return 1;
    }

    if (json) printf("[\n");
    int rc = 0;
    for (int i = utils_optind; i < argc; i++) {
        int r = stat_one(argv[i], classic, json, i == utils_optind);
        if (r) rc = r;
    }
    if (json) printf("\n]\n");
    return rc;
}
