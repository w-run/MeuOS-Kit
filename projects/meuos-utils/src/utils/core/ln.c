/* ln - 创建链接
 *
 * 支持选项：
 *   -s           符号链接
 *   -f, --force  强制（覆盖已有）
 *   -n           treat symlink-to-dir as normal file (don't follow)
 *   -r, --relative  相对路径符号链接
 *   -v, --verbose   打印每个链接
 *   --classic    POSIX 风格
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
#include <sys/types.h>
#include <unistd.h>

#include "meuos/utils.h"

static int force = 0;
static int verbose = 0;

static int do_unlink_if_exists(const char *path) {
    struct stat st;
    if (lstat(path, &st) < 0) return 0;  /* 不存在 */
    if (unlink(path) < 0) {
        fprintf(stderr, "%s: cannot remove '%s': %s\n",
                program_name, path, strerror(errno));
        return -1;
    }
    return 0;
}

static int make_link(const char *target, const char *linkpath, int sym) {
    if (force && do_unlink_if_exists(linkpath) < 0) return 1;

    int rc = 0;
    if (sym) {
        if (symlink(target, linkpath) < 0) {
            fprintf(stderr, "%s: cannot create symbolic link '%s': %s\n",
                    program_name, linkpath, strerror(errno));
            rc = 1;
        }
    } else {
        if (link(target, linkpath) < 0) {
            fprintf(stderr, "%s: cannot create hard link '%s': %s\n",
                    program_name, linkpath, strerror(errno));
            rc = 1;
        }
    }
    if (rc == 0 && verbose) {
        printf("'%s' -> '%s'\n", linkpath, target);
    }
    return rc;
}

static void usage(void) {
    fprintf(stdout,
        "Usage: %s [OPTION]... TARGET LINK_NAME\n"
        "       %s [OPTION]... TARGET... DIRECTORY\n"
        "Create a link to TARGET with the name LINK_NAME.\n\n"
        "  -s, --symbolic  make symbolic links instead of hard links\n"
        "  -f, --force     remove existing destination files\n"
        "  -v, --verbose   print name of each linked file\n"
        "      --classic   POSIX style\n"
        "      --help      display this help and exit\n"
        "      --version   output version information and exit\n",
        program_name, program_name);
    exit(0);
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    utils_classic_init(argc, argv);

    int sym = 0;

    static const struct utils_option longopts[] = {
        { "symbolic", no_argument, NULL, 's' },
        { "force",    no_argument, NULL, 'f' },
        { "verbose",  no_argument, NULL, 'v' },
        { "classic",  no_argument, NULL, 1000 },
        { "help",     no_argument, NULL, 'h' },
        { "version",  no_argument, NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };

    int opt;
    while ((opt = utils_getopt_long(argc, argv, "sfvhV", longopts, NULL)) != -1) {
        switch (opt) {
        case 's': sym = 1; break;
        case 'f': force = 1; break;
        case 'v': verbose = 1; break;
        case 1000: break;
        case 'h': usage(); return 0;
        case 'V': version(); break;
        default:
            fprintf(stderr, "%s: try --help for more information\n", program_name);
            return 2;
        }
    }

    int nargs = argc - utils_optind;
    if (nargs < 2) {
        fprintf(stderr, "%s: missing operand\n", program_name);
        return 1;
    }

    /* 最后一个若是目录，链接到目录内 */
    const char *last = argv[argc - 1];
    struct stat st;
    int last_is_dir = (stat(last, &st) == 0 && S_ISDIR(st.st_mode));

    int rc = 0;
    if (nargs == 2 && !last_is_dir) {
        rc = make_link(argv[utils_optind], argv[utils_optind + 1], sym);
    } else {
        /* 多个 target，最后一个是目录 */
        if (!last_is_dir) {
            fprintf(stderr, "%s: target '%s' is not a directory\n",
                    program_name, last);
            return 1;
        }
        for (int i = utils_optind; i < argc - 1; i++) {
            char *base = utils_basename(argv[i]);
            char buf[4096];
            snprintf(buf, sizeof(buf), "%s/%s", last, base ? base : "");
            free(base);
            int r = make_link(argv[i], buf, sym);
            if (r) rc = r;
        }
    }
    return rc;
}
