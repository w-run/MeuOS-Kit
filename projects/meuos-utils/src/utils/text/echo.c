/* echo — 打印参数到 stdout，末尾加换行（默认）
 *
 * POSIX 选项：
 *   echo [-n] [arg ...]
 *
 * 实现 GNU 扩展：
 *   -n       不输出尾随换行
 *   -e       启用反斜杠转义解释（\n \t \\ \a \b \f \r \v \0 等）
 *   -E       禁用反斜杠转义解释（默认）
 *   -h/--help / -V/--version 不在 POSIX 范围，按 GNU 习惯支持
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"

static void usage(void) {
    fprintf(stdout,
        "Usage: %s [-neE] [STRING ...]\n"
        "  -n         do not output trailing newline\n"
        "  -e         enable interpretation of backslash escapes\n"
        "  -E         disable interpretation of backslash escapes (default)\n"
        "  --help     display this help and exit\n"
        "  --version  output version information and exit\n",
        program_name);
    exit(0);
}

static void put_escape(int c) {
    switch (c) {
    case 'a': fputc('\a', stdout); break;
    case 'b': fputc('\b', stdout); break;
    case 'f': fputc('\f', stdout); break;
    case 'n': fputc('\n', stdout); break;
    case 'r': fputc('\r', stdout); break;
    case 't': fputc('\t', stdout); break;
    case 'v': fputc('\v', stdout); break;
    case '\\': fputc('\\', stdout); break;
    case '\'': fputc('\'', stdout); break;
    case '"': fputc('"', stdout); break;
    case '?': fputc('?', stdout); break;
    default:
        /* 未知转义：保留原字符（GNU 行为） */
        fputc('\\', stdout);
        fputc(c, stdout);
        break;
    }
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);

    int no_newline = 0;
    int enable_escape = 0;  /* 默认：禁用反斜杠解释（POSIX/XSI） */

    /* 解析选项。POSIX echo 不解析 --long 选项，但我们跟随 GNU，
     * 因此支持 --help/--version */
    static const struct utils_option longopts[] = {
        { "help",    0, NULL, 'h' },
        { "version", 0, NULL, 'V' },
        { NULL,      0, NULL,  0  },
    };
    int opt;
    while ((opt = utils_getopt_long(argc, argv, "neEh", longopts, NULL)) != -1) {
        switch (opt) {
        case 'n': no_newline = 1; break;
        case 'e': enable_escape = 1; break;
        case 'E': enable_escape = 0; break;
        case 'h': usage(); break;
        case 'V': version(); break;
        default:
            fprintf(stderr, "%s: try --help for more information\n",
                    program_name);
            return 2;
        }
    }

    for (int i = utils_optind; i < argc; i++) {
        const char *s = argv[i];
        if (enable_escape) {
            for (size_t j = 0; s[j]; j++) {
                if (s[j] == '\\' && s[j+1]) {
                    put_escape((unsigned char)s[j+1]);
                    j++;
                } else {
                    fputc((unsigned char)s[j], stdout);
                }
            }
        } else {
            fputs(s, stdout);
        }
        if (i + 1 < argc) fputc(' ', stdout);
    }
    if (!no_newline) fputc('\n', stdout);
    return 0;
}
