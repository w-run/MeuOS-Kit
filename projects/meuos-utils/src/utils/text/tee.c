/* tee - 从 stdin 读，写到 stdout 和各文件
 *
 * 支持选项：
 *   -a, --append  追加而非覆盖
 *   -i            忽略 SIGINT
 *   --classic     POSIX 风格（无彩色头）
 *   --help / --version
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "meuos/utils.h"

static void usage(void) {
    fprintf(stdout,
        "Usage: %s [OPTION]... [FILE]...\n"
        "Copy standard input to each FILE, and also to standard output.\n\n"
        "  -a, --append   append to the given FILEs, do not overwrite\n"
        "  -i, --ignore-interrupts  ignore interrupt signals\n"
        "      --classic  POSIX style (no colored headers)\n"
        "      --help     display this help and exit\n"
        "      --version  output version information and exit\n",
        program_name);
    exit(0);
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    utils_classic_init(argc, argv);

    int append = 0;

    static const struct utils_option longopts[] = {
        { "append",   no_argument, NULL, 'a' },
        { "ignore-interrupts", no_argument, NULL, 'i' },
        { "classic",  no_argument, NULL, 1000 },
        { "help",     no_argument, NULL, 'h' },
        { "version",  no_argument, NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };

    int opt;
    while ((opt = utils_getopt_long(argc, argv, "aihV", longopts, NULL)) != -1) {
        switch (opt) {
        case 'a': append = 1; break;
        case 'i': signal(SIGINT, SIG_IGN); break;
        case 1000: break;
        case 'h': usage(); return 0;
        case 'V': version(); break;
        default:
            fprintf(stderr, "%s: try --help for more information\n", program_name);
            return 2;
        }
    }

    int nfiles = argc - utils_optind;
    FILE **fps = NULL;
    if (nfiles > 0) {
        fps = xmalloc(sizeof(FILE *) * nfiles);
        for (int i = 0; i < nfiles; i++) {
            const char *path = argv[utils_optind + i];
            fps[i] = fopen(path, append ? "a" : "w");
            if (!fps[i]) {
                fprintf(stderr, "%s: %s: %s\n",
                        program_name, path, strerror(errno));
                fps[i] = NULL;
            }
        }
    }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
        fwrite(buf, 1, n, stdout);
        for (int i = 0; i < nfiles; i++) {
            if (fps[i]) fwrite(buf, 1, n, fps[i]);
        }
    }
    /* 刷新 */
    if (fflush(stdout) != 0) perror(program_name);
    for (int i = 0; i < nfiles; i++) {
        if (fps[i]) {
            if (fflush(fps[i]) != 0) {
                fprintf(stderr, "%s: write error: %s\n",
                        program_name, strerror(errno));
            }
            fclose(fps[i]);
        }
    }
    free(fps);
    return ferror(stdin) ? 1 : 0;
}
