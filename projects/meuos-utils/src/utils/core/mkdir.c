/* mkdir - 创建目录
 *
 * 支持选项：
 *   -p         递归创建（路径中不存在的目录全部创建）
 *   -m MODE    设置权限（八进制，如 755）
 *   --classic  POSIX 风格输出
 *   --help / --version
 *
 * 不实现：SELinux context、parents verbose 等 GNU 扩展。
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "meuos/utils.h"

static mode_t parse_mode(const char *s) {
    /* 仅支持八进制，如 "755"。不支持符号 u+x。 */
    if (!s || !*s) return (mode_t)-1;
    char *end = NULL;
    long m = strtol(s, &end, 8);
    if (*end != '\0' || m < 0 || m > 07777) return (mode_t)-1;
    return (mode_t)m;
}

/* 递归创建路径，类似 mkdir -p。 */
static int mkdir_p(const char *path, mode_t mode) {
    char *copy = xstrdup(path);
    int rc = 0;
    size_t len = strlen(copy);
    if (len == 0) { free(copy); return 1; }

    /* 去尾随 / */
    while (len > 1 && copy[len - 1] == '/') copy[--len] = '\0';

    /* 逐级创建 */
    for (size_t i = 1; i <= len; i++) {
        if (i == len || copy[i] == '/') {
            if (i < len) copy[i] = '\0';
            if (copy[0] == '\0') { if (i < len) copy[i] = '/'; continue; }
            struct stat st;
            if (stat(copy, &st) == 0) {
                if (!S_ISDIR(st.st_mode)) {
                    fprintf(stderr, "%s: cannot create directory '%s': File exists\n",
                            program_name, copy);
                    rc = 1;
                    break;
                }
            } else if (mkdir(copy, mode) < 0 && errno != EEXIST) {
                fprintf(stderr, "%s: cannot create directory '%s': %s\n",
                        program_name, copy, strerror(errno));
                rc = 1;
                break;
            }
            if (i < len) copy[i] = '/';
        }
    }
    free(copy);
    return rc;
}

static void usage(void) {
    fprintf(stdout,
        "Usage: %s [OPTION]... DIRECTORY...\n"
        "Create the DIRECTORY(ies), if they do not already exist.\n\n"
        "Mandatory arguments to long options are mandatory for short options too.\n"
        "  -m, --mode=MODE   set file mode (octal, e.g. 755)\n"
        "  -p, --parents     no error if existing, make parent directories as needed\n"
        "      --classic     POSIX style (no color, no extras)\n"
        "      --help        display this help and exit\n"
        "      --version     output version information and exit\n",
        program_name);
    exit(0);
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    utils_classic_init(argc, argv);

    int parents = 0;
    mode_t mode = 0777;
    int has_mode = 0;

    static const struct utils_option longopts[] = {
        { "mode",     required_argument, NULL, 'm' },
        { "parents",  no_argument,       NULL, 'p' },
        { "classic",  no_argument,       NULL, 1000 },
        { "help",     no_argument,       NULL, 'h' },
        { "version",  no_argument,       NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };

    int opt;
    while ((opt = utils_getopt_long(argc, argv, "m:phV", longopts, NULL)) != -1) {
        switch (opt) {
        case 'm':
            mode = parse_mode(utils_optarg);
            if (mode == (mode_t)-1) {
                fprintf(stderr, "%s: invalid mode '%s'\n", program_name, utils_optarg);
                return 1;
            }
            has_mode = 1;
            break;
        case 'p': parents = 1; break;
        case 1000: break;  /* --classic, 已由 utils_classic_init 处理 */
        case 'h': usage(); return 0;
        case 'V': version(); break;
        default:
            fprintf(stderr, "%s: try --help for more information\n", program_name);
            return 2;
        }
    }

    if (utils_optind >= argc) {
        fprintf(stderr, "%s: missing operand\n", program_name);
        fprintf(stderr, "Try '%s --help' for more information.\n", program_name);
        return 1;
    }

    int rc = 0;
    mode_t effective_mode = has_mode ? mode : 0777;
    for (int i = utils_optind; i < argc; i++) {
        int r;
        if (parents) {
            r = mkdir_p(argv[i], effective_mode);
        } else {
            if (mkdir(argv[i], effective_mode) < 0) {
                fprintf(stderr, "%s: cannot create directory '%s': %s\n",
                        program_name, argv[i], strerror(errno));
                r = 1;
            }
        }
        if (r) rc = r;
    }
    return rc;
}
