/* printf — POSIX printf 子集
 * 用法：printf FORMAT [ARGUMENT]...
 * 支持：%s %d %i %o %u %x %X %c %f %e %g %% 以及转义 \\a \\b \\f \\n \\r \\t \\v \\0NNN \\xHH
 */

#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"


static void print_str(const char *s) { fputs(s, stdout); }

static void print_escape(const char **p) {
    const char *s = *p;
    switch (*s) {
    case 'a': putchar('\a'); break;
    case 'b': putchar('\b'); break;
    case 'f': putchar('\f'); break;
    case 'n': putchar('\n'); break;
    case 'r': putchar('\r'); break;
    case 't': putchar('\t'); break;
    case 'v': putchar('\v'); break;
    case '\\': putchar('\\'); break;
    case 'c': return;  /* 特殊：停止处理，返回上层 */
    case '0': {
        /* \0NNN octal */
        int val = 0;
        int digits = 0;
        s++;
        while (digits < 3 && *s >= '0' && *s <= '7') {
            val = val * 8 + (*s - '0');
            s++; digits++;
        }
        putchar(val);
        *p = s - 1;
        break;
    }
    case 'x': {
        /* \xHH hex */
        int val = 0;
        int digits = 0;
        s++;
        while (digits < 2 && ((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f') || (*s >= 'A' && *s <= 'F'))) {
            int d;
            if (*s >= '0' && *s <= '9') d = *s - '0';
            else if (*s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
            else d = *s - 'A' + 10;
            val = val * 16 + d;
            s++; digits++;
        }
        putchar(val);
        *p = s - 1;
        break;
    }
    default:
        putchar('\\');
        putchar(*s);
        break;
    }
    (*p)++;
}

int main(int argc, char **argv) {
    if (argc > 1 && (!strcmp(argv[1], "--version"))) {
        printf("printf %s\n", version);
        return 0;
    }
    if (argc > 1 && (!strcmp(argv[1], "--help"))) {
        printf("Usage: printf FORMAT [ARGUMENT]...\n");
        return 0;
    }

    if (argc < 2) { fprintf(stderr, "printf: missing operand\n"); return 2; }

    const char *fmt = argv[1];
    int argi = 2;

    for (const char *p = fmt; *p; p++) {
        if (*p == '\\') {
            p++;
            if (!*p) { putchar('\\'); break; }
            const char *pp = p;
            if (*pp == 'c') {
                /* 停止并返回：后续不处理 */
                return 0;
            }
            print_escape(&p);
            continue;
        }
        if (*p == '%') {
            p++;
            if (!*p) { putchar('%'); break; }
            if (*p == '%') { putchar('%'); continue; }
            /* 宽度/精度简化：跳过 */
            while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') p++;
            while (isdigit((unsigned char)*p)) p++;
            if (*p == '.') {
                p++;
                while (isdigit((unsigned char)*p)) p++;
            }
            /* 长度修饰符 */
            if (*p == 'l') p++;
            if (*p == 'l') p++;
            char spec = *p;
            const char *arg = (argi < argc) ? argv[argi] : "";
            argi++;
            char buf[256];
            switch (spec) {
            case 's': print_str(arg); break;
            case 'd': case 'i': snprintf(buf, sizeof(buf), "%d", atoi(arg)); print_str(buf); break;
            case 'o': snprintf(buf, sizeof(buf), "%o", (unsigned)atoi(arg)); print_str(buf); break;
            case 'u': snprintf(buf, sizeof(buf), "%u", (unsigned)atoi(arg)); print_str(buf); break;
            case 'x': snprintf(buf, sizeof(buf), "%x", (unsigned)atoi(arg)); print_str(buf); break;
            case 'X': snprintf(buf, sizeof(buf), "%X", (unsigned)atoi(arg)); print_str(buf); break;
            case 'c': {
                int c = arg[0];
                putchar(c);
                break;
            }
            case 'f': case 'e': case 'E': case 'g': case 'G': {
                snprintf(buf, sizeof(buf), "%f", atof(arg));
                print_str(buf);
                break;
            }
            case 'p': snprintf(buf, sizeof(buf), "%p", (void*)arg); print_str(buf); break;
            default:
                putchar('%');
                putchar(spec);
                break;
            }
            continue;
        }
        putchar(*p);
    }
    return 0;
}
