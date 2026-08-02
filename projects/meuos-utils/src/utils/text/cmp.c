/* cmp - 逐字节比较两个文件
 *
 * 支持选项：
 *   -l, --verbose  输出所有差异的字节位置和值
 *   -s, --silent   不输出，仅通过退出码（0=相同/1=不同/2=错误）
 *   --classic      POSIX 风格
 *   --help / --version
 *
 * 退出码：0=相同，1=不同，2=错误
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"

static void usage(void) {
    fprintf(stdout,
        "Usage: %s [OPTION]... FILE1 FILE2\n"
        "Compare two files byte by byte.\n\n"
        "  -l, --verbose   output byte positions and differing byte values\n"
        "  -s, --silent    suppress all normal output (use exit code only)\n"
        "      --classic   POSIX style\n"
        "      --help       display this help and exit\n"
        "      --version    output version information and exit\n",
        program_name);
    exit(0);
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    utils_classic_init(argc, argv);

    int verbose = 0;
    int silent = 0;

    static const struct utils_option longopts[] = {
        { "verbose", no_argument, NULL, 'l' },
        { "silent",  no_argument, NULL, 's' },
        { "quiet",   no_argument, NULL, 's' },
        { "classic", no_argument, NULL, 1000 },
        { "help",    no_argument, NULL, 'h' },
        { "version", no_argument, NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };

    int opt;
    while ((opt = utils_getopt_long(argc, argv, "lshV", longopts, NULL)) != -1) {
        switch (opt) {
        case 'l': verbose = 1; break;
        case 's': silent = 1; break;
        case 1000: break;
        case 'h': usage(); return 0;
        case 'V': version(); break;
        default:
            fprintf(stderr, "%s: try --help for more information\n", program_name);
            return 2;
        }
    }

    if (argc - utils_optind < 2) {
        fprintf(stderr, "%s: missing operand\n", program_name);
        return 2;
    }

    const char *f1 = argv[utils_optind];
    const char *f2 = argv[utils_optind + 1];
    FILE *fp1 = strcmp(f1, "-") == 0 ? stdin : fopen(f1, "rb");
    FILE *fp2 = strcmp(f2, "-") == 0 ? stdin : fopen(f2, "rb");
    if (!fp1) {
        fprintf(stderr, "%s: %s: %s\n", program_name, f1, strerror(errno));
        return 2;
    }
    if (!fp2) {
        fprintf(stderr, "%s: %s: %s\n", program_name, f2, strerror(errno));
        if (fp1 != stdin) fclose(fp1);
        return 2;
    }

    long pos = 1;
    long line = 1;
    int diff_found = 0;
    int c1, c2;
    while (1) {
        c1 = fgetc(fp1);
        c2 = fgetc(fp2);
        if (c1 == EOF || c2 == EOF) break;
        if (c1 != c2) {
            if (!silent) {
                if (verbose) {
                    printf("%ld %o %o\n", pos, (unsigned char)c1, (unsigned char)c2);
                } else {
                    printf("%s %s differ: byte %ld, line %ld\n",
                           f1, f2, pos, line);
                    /* 默认只输出首个差异 */
                    diff_found = 1;
                    break;
                }
            }
            diff_found = 1;
        }
        if (c1 == '\n') line++;
        pos++;
    }

    /* 检查长度差异 */
    if (!diff_found) {
        if (c1 == EOF && c2 != EOF) {
            if (!silent) {
                if (verbose) {
                    /* 继续输出剩余差异 */
                } else {
                    printf("%s %s differ: byte %ld, line %ld\n",
                           f1, f2, pos, line);
                }
            }
            diff_found = 1;
        } else if (c1 != EOF && c2 == EOF) {
            if (!silent) {
                if (!verbose) {
                    printf("%s %s differ: byte %ld, line %ld\n",
                           f1, f2, pos, line);
                }
            }
            diff_found = 1;
        }
    }

    if (fp1 != stdin) fclose(fp1);
    if (fp2 != stdin) fclose(fp2);

    return diff_found ? 1 : 0;
}
