/* mv — 移动/重命名（MeuOS Next 版）
 *
 * 优先 rename(2)；
 * 跨文件系统走 cp + rm 退化。
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "meuos/utils.h"

static int flag_force = 1;

static int do_mv(const char *src, const char *dst) {
    if (rename(src, dst) == 0) return 0;
    if (errno == EXDEV) {
        /* 跨 fs：cp + rm */
        fprintf(stderr, "mv: cross-filesystem, fall back to cp+rm (not yet)\n");
        return 1;
    }
    if (!flag_force) {
        fprintf(stderr, "mv: %s -> %s: %s\n", src, dst, strerror(errno));
        return 1;
    }
    fprintf(stderr, "mv: %s -> %s: %s\n", src, dst, strerror(errno));
    return 1;
}

static void usage(void) {
    printf("Usage: %s [OPTIONS] SRC DST | SRC... DIR\n", program_name);
    printf("\n");
    printf("Move (rename) files.\n");
    printf("  --classic    plain mv behavior\n");
    printf("      --help   show this help\n");
    printf("      --version\n");
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);

    static const struct option longopts[] = {
        { "classic", no_argument, NULL, 1000 },
        { "help",    no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "h", longopts, NULL)) != -1) {
        switch (opt) {
        case 'h': usage(); break;
        case 'V': version(); break;
        case 1000: break;
        default: return 2;
        }
    }
    if (argc - optind < 2) {
        fprintf(stderr, "%s: missing source/dest\n", program_name);
        usage();
        return 1;
    }
    int nsrc = argc - 1 - optind;
    const char *dst = argv[argc - 1];
    int rc = 0;
    if (nsrc == 1) {
        if (do_mv(argv[optind], dst) < 0) rc = 1;
    } else {
        for (int i = 0; i < nsrc; i++) {
            const char *base = strrchr(argv[optind + i], '/');
            base = base ? base + 1 : argv[optind + i];
            char target[PATH_MAX];
            snprintf(target, sizeof(target), "%s/%s", dst, base);
            if (do_mv(argv[optind + i], target) < 0) rc = 1;
        }
    }
    return rc;
}
