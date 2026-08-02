/* expr — 表达式求值
 * 用法：expr EXPRESSION
 * 支持：整数算术(+ - * / %), 比较(= != < <= > >=), 字符串操作
 *       逻辑(& |), match, substr, length, index
 * 退出码：0=真/非空, 1=假/空, 2=错误
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "meuos/utils.h"


/* 简化版递归下降表达式求值器 */

typedef struct {
    char **argv;
    int argc;
    int pos;
} parser_t;

static char *get_tok(parser_t *p) {
    if (p->pos >= p->argc) return NULL;
    return p->argv[p->pos];
}

static char *next_tok(parser_t *p) {
    if (p->pos >= p->argc) return NULL;
    return p->argv[p->pos++];
}

static void putback(parser_t *p) { if (p->pos > 0) p->pos--; }

static int is_integer(const char *s) {
    if (!s || !*s) return 0;
    if (*s == '-') s++;
    if (!*s) return 0;
    for (; *s; s++) if (!isdigit((unsigned char)*s)) return 0;
    return 1;
}

static char *int_to_str(long val) {
    char *r = malloc(32);
    snprintf(r, 32, "%ld", val);
    return r;
}

static char *eval_or(parser_t *p);

static char *eval_primary(parser_t *p) {
    char *t = next_tok(p);
    if (!t) return strdup("");
    /* match, substr, length, index */
    if (!strcmp(t, "match")) {
        char *str = next_tok(p);
        char *pat = next_tok(p);
        /* 简化：返回匹配的前缀长度 */
        if (!str || !pat) return strdup("0");
        /* 正则匹配简化为子串查找 */
        char *found = strstr(str, pat);
        if (found) return int_to_str(strlen(pat));
        return strdup("0");
    }
    if (!strcmp(t, "substr")) {
        char *str = next_tok(p);
        char *pos_s = next_tok(p);
        char *len_s = next_tok(p);
        if (!str || !pos_s || !len_s) return strdup("");
        int pos = atoi(pos_s) - 1;
        int len = atoi(len_s);
        if (pos < 0) pos = 0;
        if (pos >= (int)strlen(str)) return strdup("");
        if (len < 0) len = 0;
        if (pos + len > (int)strlen(str)) len = strlen(str) - pos;
        char *r = malloc(len + 1);
        memcpy(r, str + pos, len);
        r[len] = '\0';
        return r;
    }
    if (!strcmp(t, "length")) {
        char *str = next_tok(p);
        if (!str) return strdup("0");
        return int_to_str(strlen(str));
    }
    if (!strcmp(t, "index")) {
        char *str = next_tok(p);
        char *chars = next_tok(p);
        if (!str || !chars) return strdup("0");
        for (int i = 0; str[i]; i++) {
            if (strchr(chars, str[i])) return int_to_str(i + 1);
        }
        return strdup("0");
    }
    if (!strcmp(t, "+")) return strdup("+");  /* 一元 + */
    /* 括号 */
    if (!strcmp(t, "(")) {
        char *r = eval_or(p);
        char *close = next_tok(p);
        (void)close;  /* 期望 ')' */
        return r;
    }
    return strdup(t);
}

static char *eval_mul(parser_t *p) {
    char *left = eval_primary(p);
    while (1) {
        char *t = get_tok(p);
        if (!t) break;
        if (strcmp(t, "*") == 0 || strcmp(t, "/") == 0 || strcmp(t, "%") == 0) {
            p->pos++;
            char *right = eval_primary(p);
            if (is_integer(left) && is_integer(right)) {
                long a = atol(left), b = atol(right);
                long r;
                if (*t == '*') r = a * b;
                else if (*t == '/') r = b ? a / b : 0;
                else r = b ? a % b : 0;
                free(left); free(right);
                left = int_to_str(r);
            } else {
                free(right);
            }
        } else break;
    }
    return left;
}

static char *eval_add(parser_t *p) {
    char *left = eval_mul(p);
    while (1) {
        char *t = get_tok(p);
        if (!t) break;
        if (strcmp(t, "+") == 0 || strcmp(t, "-") == 0) {
            p->pos++;
            char *right = eval_mul(p);
            if (is_integer(left) && is_integer(right)) {
                long a = atol(left), b = atol(right);
                long r = (*t == '+') ? a + b : a - b;
                free(left); free(right);
                left = int_to_str(r);
            } else {
                /* 字符串拼接(仅 +) */
                if (*t == '+') {
                    size_t la = strlen(left), lb = strlen(right);
                    char *r = malloc(la + lb + 1);
                    memcpy(r, left, la);
                    memcpy(r + la, right, lb);
                    r[la + lb] = '\0';
                    free(left); free(right);
                    left = r;
                } else { free(right); }
            }
        } else break;
    }
    return left;
}

static char *eval_cmp(parser_t *p) {
    char *left = eval_add(p);
    char *t = get_tok(p);
    if (t && (strcmp(t, "=") == 0 || strcmp(t, "==") == 0 || strcmp(t, "!=") == 0 ||
              strcmp(t, "<") == 0 || strcmp(t, "<=") == 0 ||
              strcmp(t, ">") == 0 || strcmp(t, ">=") == 0)) {
        p->pos++;
        char *right = eval_add(p);
        int r;
        if (is_integer(left) && is_integer(right)) {
            long a = atol(left), b = atol(right);
            if (!strcmp(t,"=")||!strcmp(t,"==")) r = (a == b);
            else if (!strcmp(t,"!=")) r = (a != b);
            else if (!strcmp(t,"<")) r = (a < b);
            else if (!strcmp(t,"<=")) r = (a <= b);
            else if (!strcmp(t,">")) r = (a > b);
            else r = (a >= b);
        } else {
            int cmp = strcmp(left, right);
            if (!strcmp(t,"=")||!strcmp(t,"==")) r = (cmp == 0);
            else if (!strcmp(t,"!=")) r = (cmp != 0);
            else if (!strcmp(t,"<")) r = (cmp < 0);
            else if (!strcmp(t,"<=")) r = (cmp <= 0);
            else if (!strcmp(t,">")) r = (cmp > 0);
            else r = (cmp >= 0);
        }
        free(left); free(right);
        return r ? strdup("1") : strdup("0");
    }
    return left;
}

static char *eval_and(parser_t *p) {
    char *left = eval_cmp(p);
    char *t = get_tok(p);
    if (t && strcmp(t, "&") == 0) {
        p->pos++;
        if (left[0] == '\0' || (!strcmp(left, "0"))) { free(left); return strdup("0"); }
        free(left);
        return eval_and(p);
    }
    return left;
}

static char *eval_or(parser_t *p) {
    char *left = eval_and(p);
    char *t = get_tok(p);
    if (t && strcmp(t, "|") == 0) {
        p->pos++;
        if (left[0] != '\0' && strcmp(left, "0")) { free(left); return strdup(left); }
        free(left);
        return eval_or(p);
    }
    return left;
}

int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    if (argi < argc && !strcmp(argv[argi], "--help")) { printf("Usage: expr EXPRESSION\n"); return 0; }
    if (argc < 2) { fprintf(stderr, "expr: missing operand\n"); return 2; }
    /* 跳过 -- */
    int start = 1;
    if (!strcmp(argv[1], "--")) start = 2;
    parser_t p = { .argv = argv + start, .argc = argc - start, .pos = 0 };
    char *result = eval_or(&p);
    int rc;
    if (result[0] == '\0' || (!strcmp(result, "0"))) rc = 1;
    else rc = 0;
    puts(result);
    free(result);
    return rc;
}
