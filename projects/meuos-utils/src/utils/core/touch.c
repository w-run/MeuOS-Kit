/* touch - 更新文件时间戳，或创建空文件
 *
 * 支持选项：
 *   -a           仅改 atime
 *   -m           仅改 mtime
 *   -c, --no-create  不创建文件
 *   -r FILE      用 FILE 的时间戳
 *   -t TIME      用 [[CC]YY]MMDDhhmm[.ss] 设置时间
 *   --classic    POSIX 风格
 *   --help / --version
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "meuos/utils.h"

static void usage(void) {
    fprintf(stdout,
        "Usage: %s [OPTION]... FILE...\n"
        "Update the access and modification times of each FILE to the current time.\n\n"
        "  -a            change only the access time\n"
        "  -m            change only the modification time\n"
        "  -c, --no-create  do not create any files\n"
        "  -r, --reference=FILE  use this file's times instead of current time\n"
        "  -t STAMP      use [[CC]YY]MMDDhhmm[.ss] instead of current time\n"
        "      --classic POSIX style\n"
        "      --help    display this help and exit\n"
        "      --version output version information and exit\n",
        program_name);
    exit(0);
}

static int parse_stamp(const char *s, struct timespec *ts) {
    /* 简化：仅支持 YYYYMMDDhhmm 或 MMDDhhmm */
    struct tm tm = {0};
    char buf[32];
    size_t L = strlen(s);
    if (L >= sizeof(buf)) return -1;
    memcpy(buf, s, L + 1);

    char *dot = strchr(buf, '.');
    int sec = 0;
    if (dot) {
        *dot = '\0';
        sec = atoi(dot + 1);
    }
    if (strlen(buf) == 12) {
        /* MMDDhhmm */
        int M, D, h, m;
        if (sscanf(buf, "%2d%2d%2d%2d", &M, &D, &h, &m) != 4) return -1;
        time_t now = time(NULL);
        struct tm now_tm = *localtime(&now);
        tm = now_tm;
        tm.tm_mon = M - 1; tm.tm_mday = D;
        tm.tm_hour = h; tm.tm_min = m; tm.tm_sec = sec;
        tm.tm_isdst = -1;
    } else if (strlen(buf) == 14) {
        /* YYYYMMDDhhmm */
        int Y, M, D, h, m;
        if (sscanf(buf, "%4d%2d%2d%2d%2d", &Y, &M, &D, &h, &m) != 5) return -1;
        tm.tm_year = Y - 1900; tm.tm_mon = M - 1; tm.tm_mday = D;
        tm.tm_hour = h; tm.tm_min = m; tm.tm_sec = sec;
        tm.tm_isdst = -1;
    } else {
        return -1;
    }
    ts->tv_sec = mktime(&tm);
    ts->tv_nsec = 0;
    return 0;
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    utils_classic_init(argc, argv);

    int change_atime = 1, change_mtime = 1;
    int no_create = 0;
    const char *ref_file = NULL;
    const char *stamp = NULL;

    static const struct utils_option longopts[] = {
        { "no-create", no_argument,       NULL, 'c' },
        { "reference", required_argument, NULL, 'r' },
        { "classic",   no_argument,       NULL, 1000 },
        { "help",      no_argument,       NULL, 'h' },
        { "version",   no_argument,       NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };

    int opt;
    while ((opt = utils_getopt_long(argc, argv, "amcr:t:hV", longopts, NULL)) != -1) {
        switch (opt) {
        case 'a': change_mtime = 0; break;
        case 'm': change_atime = 0; break;
        case 'c': no_create = 1; break;
        case 'r': ref_file = utils_optarg; break;
        case 't': stamp = utils_optarg; break;
        case 1000: break;
        case 'h': usage(); return 0;
        case 'V': version(); break;
        default:
            fprintf(stderr, "%s: try --help for more information\n", program_name);
            return 2;
        }
    }

    if (utils_optind >= argc) {
        fprintf(stderr, "%s: missing file operand\n", program_name);
        return 1;
    }

    struct timespec times[2];
    if (stamp) {
        if (parse_stamp(stamp, &times[0]) < 0) {
            fprintf(stderr, "%s: invalid date format '%s'\n", program_name, stamp);
            return 1;
        }
        times[1] = times[0];
    } else if (ref_file) {
        struct stat st;
        if (stat(ref_file, &st) < 0) {
            fprintf(stderr, "%s: failed to get attributes of '%s': %s\n",
                    program_name, ref_file, strerror(errno));
            return 1;
        }
        times[0] = st.st_atim;
        times[1] = st.st_mtim;
    } else {
        times[0].tv_nsec = UTIME_NOW;
        times[1].tv_nsec = UTIME_NOW;
    }

    if (!change_atime) times[0].tv_nsec = UTIME_OMIT;
    if (!change_mtime) times[1].tv_nsec = UTIME_OMIT;

    int rc = 0;
    for (int i = utils_optind; i < argc; i++) {
        const char *path = argv[i];
        struct stat st;
        if (stat(path, &st) < 0) {
            if (errno != ENOENT) {
                fprintf(stderr, "%s: cannot stat '%s': %s\n",
                        program_name, path, strerror(errno));
                rc = 1;
                continue;
            }
            if (no_create) continue;
            /* 创建空文件 */
            int fd = open(path, O_CREAT | O_WRONLY, 0666);
            if (fd < 0) {
                fprintf(stderr, "%s: cannot touch '%s': %s\n",
                        program_name, path, strerror(errno));
                rc = 1;
                continue;
            }
            close(fd);
            /* 创建后再设时间戳 */
            if (utimensat(AT_FDCWD, path, times, 0) < 0) {
                fprintf(stderr, "%s: setting times of '%s': %s\n",
                        program_name, path, strerror(errno));
                rc = 1;
            }
        } else {
            if (utimensat(AT_FDCWD, path, times, 0) < 0) {
                fprintf(stderr, "%s: setting times of '%s': %s\n",
                        program_name, path, strerror(errno));
                rc = 1;
            }
        }
    }
    return rc;
}
