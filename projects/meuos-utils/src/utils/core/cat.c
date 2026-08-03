/* cat - 现代 cat（bat-lite，MeuOS Next 版）
 *
 * 默认：行号 + 语法着色（按扩展名自动检测）+ 文件名头（多文件）+ JSON 漂亮打印。
 * --classic: 纯 cat，无任何增强。
 * --no-number: 关掉默认行号。
 *
 * POSIX 字符渲染（与 GNU cat 行为一致）：
 *   -E  行尾显示 $
 *   -T  TAB 显示 ^I
 *   -v  非打印字符显示为 ^X（< 0x20 且非 \t/\n）或 M-X（>= 0x80）
 *   -A  等价 -vET
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "meuos/color.h"
#include "meuos/json.h"
#include "meuos/syntax.h"
#include "meuos/utils.h"

static int flag_show_ends = 0;
static int flag_show_tabs = 0;
static int flag_show_nonprint = 0;
static int flag_number = 1;          /* 默认开 */
static int flag_number_nonblank = 0;
static int flag_squeeze = 0;
static int flag_classic = 0;
static int flag_no_color = 0;

static void usage(void) {
    printf("Usage: %s [OPTIONS] [FILE...]\n\n", program_name);
    printf("Modern cat with line numbers and syntax highlight.\n");
    printf("Default mode: line numbers + syntax color + file headers.\n\n");
    printf("  -n, --number              number all lines (default: on)\n");
    printf("      --no-number           disable line numbers\n");
    printf("  -b, --number-nonblank     number non-empty lines\n");
    printf("  -E, --show-ends           display $ at end of each line\n");
    printf("  -T, --show-tabs           display TAB as ^I\n");
    printf("  -v, --show-nonprinting    show non-printing chars as ^X / M-X\n");
    printf("  -A, --show-all            = -vET\n");
    printf("  -s, --squeeze-blank       compress repeated empty lines\n");
    printf("      --no-color            disable color\n");
    printf("      --classic             plain cat (no number, no color)\n");
    printf("      --help                show this help\n");
    printf("      --version             show version\n");
}

static int is_known_code_path(const char *path) {
    syn_language_t lang = syn_detect(path);
    return lang != SYN_LANG_UNKNOWN && lang != SYN_LANG_DIFF;
}

static void print_pretty_json(FILE *fp, const char *path) {
    (void)path;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    if (sz <= 0 || sz > 1 << 22) return;  /* > 4MB 跳过 */
    fseek(fp, 0, SEEK_SET);
    char *buf = xmalloc((size_t)sz + 1);
    fread(buf, 1, (size_t)sz, fp);
    buf[sz] = '\0';
    json_value_t *jv = json_parse(buf, (size_t)sz);
    if (jv) {
        json_pretty(jv, stdout, 2);
        json_value_free(jv);
    } else {
        fputs(buf, stdout);
    }
    free(buf);
}

/* 渲染一个字节到 out，按 -E/-T/-v 规则。返回写入字节数。
 * is_newline_byte: 当前字节是否为 \n（外部已处理行尾逻辑）。 */
static void emit_byte(unsigned char b, FILE *out) {
    if (b == '\t') {
        if (flag_show_tabs) { fputs("^I", out); return; }
        fputc('\t', out);
        return;
    }
    if (b == '\n') {
        if (flag_show_ends) fputc('$', out);
        fputc('\n', out);
        return;
    }
    int need_v = flag_show_nonprint;
    if (!need_v) {
        fputc(b, out);
        return;
    }
    /* -v 模式：非打印字符 */
    if (b < 0x20) {
        /* 控制字符 ^X (X = b + 64) */
        fputc('^', out);
        fputc((char)(b + 64), out);
    } else if (b == 0x7f) {
        fputs("^?", out);
    } else if (b >= 0x80) {
        /* M-X：高位置 0 后输出（近似 UTF-8 单字节处理） */
        fputc('M', out);
        fputc('-', out);
        unsigned char low = b & 0x7f;
        if (low < 0x20) {
            fputc('^', out);
            fputc((char)(low + 64), out);
        } else if (low == 0x7f) {
            fputs("^?", out);
        } else {
            fputc((char)low, out);
        }
    } else {
        fputc((char)b, out);
    }
}

/* 用 -E/-T/-v 规则渲染一行（不含尾 \n，已剥离）到 out。
 * 高亮路径不应用字符渲染（高亮自身的输出已是 ANSI）。
 * 行尾 $ 由调用方在 \n 前输出。 */
static void render_plain_line(const char *buf, size_t len, FILE *out) {
    for (size_t i = 0; i < len; i++) {
        emit_byte((unsigned char)buf[i], out);
    }
}

static void cat_one(FILE *fp, const char *path) {
    int use_render = flag_show_ends || flag_show_tabs || flag_show_nonprint;

    /* 字符渲染模式：朴素输出，无文件头/行号/高亮（POSIX 兼容） */
    if (use_render) {
        char buf[8192];
        while (fgets(buf, sizeof(buf), fp)) {
            size_t len = strlen(buf);
            for (size_t i = 0; i < len; i++) {
                emit_byte((unsigned char)buf[i], stdout);
            }
        }
        return;
    }

    /* 文件头（多文件） */
    if (path && !flag_classic) {
        if (flag_no_color || !color_enabled) {
            printf("==> %s <==\n", path);
        } else {
            printf("\033[1m==> %s <==\033[0m\n", path);
        }
    }

    /* JSON 自动 pretty-print（仅 modern 模式 + 已知扩展名） */
    if (!flag_classic && is_known_code_path(path)
        && (syn_detect(path) == SYN_LANG_JSON)) {
        print_pretty_json(fp, path);
        return;
    }

    syn_language_t lang = flag_classic ? SYN_LANG_UNKNOWN : syn_detect(path);
    int use_color = !flag_no_color && !flag_classic && color_enabled;
    int lineno = 1;
    char buf[8192];
    int prev_blank = 0;

    while (fgets(buf, sizeof(buf), fp)) {
        size_t len = strlen(buf);
        int blank = (len == 1 && buf[0] == '\n');
        if (flag_squeeze && blank && prev_blank) continue;
        prev_blank = blank;

        /* classic 纯透传 */
        if (flag_classic) {
            fputs(buf, stdout);
            continue;
        }

        size_t body_len = (len > 0 && buf[len - 1] == '\n') ? len - 1 : len;

        if (flag_number && !(flag_number_nonblank && blank)) {
            if (use_color) printf("\033[36m%6d\033[0m  ", lineno);
            else printf("%6d  ", lineno);
            lineno++;
        } else if (flag_number_nonblank && !blank) {
            if (use_color) printf("\033[36m%6d\033[0m  ", lineno);
            else printf("%6d  ", lineno);
            lineno++;
        }

        if (use_color && lang != SYN_LANG_UNKNOWN) {
            syntax_highlight_line(lang, buf, body_len, stdout, 1);
            if (len > 0 && buf[len - 1] == '\n') fputc('\n', stdout);
        } else {
            fwrite(buf, 1, len, stdout);
        }
    }
}

int main(int argc, char **argv) {
    set_program_name(argv[0]);
    color_enable();

    static const struct option longopts[] = {
        { "number",           no_argument,       NULL, 'n' },
        { "no-number",        no_argument,       NULL, 1000 },
        { "number-nonblank",  no_argument,       NULL, 'b' },
        { "show-ends",        no_argument,       NULL, 'E' },
        { "show-tabs",        no_argument,       NULL, 'T' },
        { "show-nonprinting", no_argument,       NULL, 'v' },
        { "show-all",         no_argument,       NULL, 'A' },
        { "squeeze-blank",    no_argument,       NULL, 's' },
        { "no-color",         no_argument,       NULL, 1001 },
        { "classic",          no_argument,       NULL, 1002 },
        { "help",             no_argument,       NULL, 'h' },
        { "version",          no_argument,       NULL, 'V' },
        { NULL, 0, NULL, 0 },
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "nbETvAsh", longopts, NULL)) != -1) {
        switch (opt) {
        case 'n': flag_number = 1; break;
        case 1000: flag_number = 0; break;
        case 'b': flag_number_nonblank = 1; flag_number = 0; break;
        case 'E': flag_show_ends = 1; break;
        case 'T': flag_show_tabs = 1; break;
        case 'v': flag_show_nonprint = 1; break;
        case 'A': flag_show_ends = flag_show_tabs = flag_show_nonprint = 1; break;
        case 's': flag_squeeze = 1; break;
        case 1001: color_disable(); flag_no_color = 1; break;
        case 1002:
            flag_classic = 1;
            flag_number = 0;
            color_disable();
            flag_no_color = 1;
            break;
        case 'h': usage(); return 0;
        case 'V': version(); break;
        default: return 2;
        }
    }

    int bad = 0;
    if (optind >= argc) {
        cat_one(stdin, NULL);
    } else {
        for (int i = optind; i < argc; i++) {
            const char *name = argv[i];
            if (strcmp(name, "-") == 0) {
                cat_one(stdin, "<stdin>");
                clearerr(stdin);
                continue;
            }
            FILE *fp = fopen(name, "r");
            if (!fp) {
                fprintf(stderr, "%s: %s: %s\n", program_name, name, strerror(errno));
                bad = 1;
                continue;
            }
            cat_one(fp, name);
            fclose(fp);
        }
    }
    return bad ? 1 : 0;
}
