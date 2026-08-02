/* rmdir - 删除空目录
 *
 * 支持选项：
 *   -p, --parents  递归删除父目录（类似 mkdir -p 的逆操作）
 *   --classic      POSIX 风格
 *   --help / --version
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "meuos/utils.h"

static void usage(void) {
    fprintf(stdout,
        "Usage: %s [OPTION]... DIRECTORY...\n"
        "Remove the DIRECTORY(ies), if they are empty.\n\n"
        "  -p, --parents   remove DIRECTORY and its ancestors; e.g.,\n"
        "                  'rmdir -p a/b/c' is similar to 'rmdir a/b/c a/b a'\n"
        "      --classic   POSIX style\n"
        "      --help      display this help and exit\n"
        "      --version   output version information and exit\n",
        program_name);
    exit(0);
}

static int rmdir_one(const char *path) {
    if (rmdir(path) < 0) {
        fprintf(stderr, "%s: failed to remove '%s': %s\n",
                program_name, path, strerror(errno));
        return 1;
    }
    return 0;
}

static int rmdir_p(const char *path) {
    /* 逐级向上删除，遇到非空目录就停止（不报错） */
    char *copy = xstrdup(path);
    int rc = 0;
    size_t len = strlen(copy);
    while (len > 0) {
        while (len > 1 && copy[len - 1] == '/') copy[--len] = '\0';
        if (rmdir(copy) < 0) {
            if (errno == ENOTEMPTY || errno == EEXIST) {
                /* 父目录非空，停止递归 */
                break;
            }
            fprintf(stderr, "%s: failed to remove '%s': %s\n",
                    program_name, copy, strerror(errno));
            rc = 1;
            break;
        }
        /* 找上一个 '/' */
        char *slash = strrchr(copy, '/');
        if (!slash) break;
        *slash = '\0';
        len = strlen(copy);
        if (len == 0) break;
    }
    free(copy);
    return rc;
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    utils_classic_init(argc, argv);

    int parents = 0;

    static const struct utils_option longopts[] = {
        { "parents", no_argument, NULL, 'p' },
        { "classic", no_argument, NULL, 1000 },
        { "help",    no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };

    int opt;
    while ((opt = utils_getopt_long(argc, argv, "phV", longopts, NULL)) != -1) {
        switch (opt) {
        case 'p': parents = 1; break;
        case 1000: break;
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

    int rc = 0;
    for (int i = utils_optind; i < argc; i++) {
        int r;
        if (parents) r = rmdir_p(argv[i]);
        else         r = rmdir_one(argv[i]);
        if (r) rc = r;
    }
    return rc;
}
