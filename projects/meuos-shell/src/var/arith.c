/* msh/var/arith.c - $((...)) 算术求值器
 *
 * 递归下降：
 *   expr  := logor
 *   logor := logand ('||' logand)*
 *   logand:= eq ('&&' eq)*
 *   eq    := rel (('==' | '!=') rel)*
 *   rel   := add (('<' | '>' | '<=' | '>=') add)*
 *   add   := term (('+' | '-') term)*
 *   term  := factor (('*' | '/' | '%') factor)*
 *   factor:= number | $VAR | ${VAR} | ( expr ) | ('-' | '!') factor
 *
 * 不实现：= += ++ -- 三元 ?: 位运算 & | ^ ~ << >>
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "msh/expand.h"
#include "msh/exec.h"

typedef struct {
    const char *p;
    const char *err;
} arith_state;

static long arith_expr(arith_state *st);

static void skip_ws(arith_state *st) {
    while (*st->p && isspace((unsigned char)*st->p)) st->p++;
}

static long read_var(arith_state *st) {
    /* 调用方已消费 '$'，当前指向 VAR 首字符或 '{' */
    char name[128];
    size_t nl = 0;
    if (*st->p == '{') {
        st->p++;
        while (*st->p && *st->p != '}' && nl < sizeof(name) - 1) {
            name[nl++] = *st->p++;
        }
        if (*st->p == '}') st->p++;
    } else {
        while ((isalnum((unsigned char)*st->p) || *st->p == '_') && nl < sizeof(name) - 1) {
            name[nl++] = *st->p++;
        }
    }
    name[nl] = '\0';
    const char *v = getenv(name);
    if (!v || !*v) return 0;
    return strtol(v, NULL, 10);
}

static long arith_factor(arith_state *st) {
    skip_ws(st);
    char c = *st->p;
    if (c == '-') { st->p++; return -arith_factor(st); }
    if (c == '+') { st->p++; return arith_factor(st); }
    if (c == '!') { st->p++; return !arith_factor(st); }
    if (c == '(') {
        st->p++;
        long v = arith_expr(st);
        skip_ws(st);
        if (*st->p == ')') st->p++;
        else if (!st->err) st->err = "expected )";
        return v;
    }
    if (c == '$') {
        st->p++;
        return read_var(st);
    }
    if (isdigit((unsigned char)c)) {
        char *end;
        long v = strtol(st->p, &end, 10);
        st->p = end;
        return v;
    }
    if (isalpha((unsigned char)c) || c == '_') {
        /* 裸标识符：按变量取值（bash 行为） */
        char name[128];
        size_t nl = 0;
        while ((isalnum((unsigned char)*st->p) || *st->p == '_') && nl < sizeof(name) - 1) {
            name[nl++] = *st->p++;
        }
        name[nl] = '\0';
        const char *v = getenv(name);
        if (!v || !*v) return 0;
        return strtol(v, NULL, 10);
    }
    if (!st->err) {
        /* tolerate empty factor as 0 to keep forgiving */
        return 0;
    }
    return 0;
}

static long arith_term(arith_state *st) {
    long v = arith_factor(st);
    while (1) {
        skip_ws(st);
        char c = *st->p;
        if (c == '*' || c == '/' || c == '%') {
            st->p++;
            long r = arith_factor(st);
            if (c == '*') v *= r;
            else if (r == 0) { if (!st->err) st->err = "division by zero"; return 0; }
            else if (c == '/') v /= r;
            else v %= r;
        } else {
            break;
        }
    }
    return v;
}

static long arith_add(arith_state *st) {
    long v = arith_term(st);
    while (1) {
        skip_ws(st);
        char c = *st->p;
        if (c == '+' || c == '-') {
            st->p++;
            long r = arith_term(st);
            v = (c == '+') ? v + r : v - r;
        } else {
            break;
        }
    }
    return v;
}

static long arith_rel(arith_state *st) {
    long v = arith_add(st);
    while (1) {
        skip_ws(st);
        char c = *st->p;
        char c2 = st->p[1];
        if ((c == '<' && c2 == '=') || (c == '>' && c2 == '=')) {
            st->p += 2;
            long r = arith_add(st);
            v = (c == '<') ? (v <= r) : (v >= r);
        } else if (c == '<' || c == '>') {
            st->p++;
            long r = arith_add(st);
            v = (c == '<') ? (v < r) : (v > r);
        } else {
            break;
        }
    }
    return v;
}

static long arith_eq(arith_state *st) {
    long v = arith_rel(st);
    while (1) {
        skip_ws(st);
        char c = *st->p;
        char c2 = st->p[1];
        if ((c == '=' && c2 == '=') || (c == '!' && c2 == '=')) {
            st->p += 2;
            long r = arith_rel(st);
            v = (c == '=') ? (v == r) : (v != r);
        } else {
            break;
        }
    }
    return v;
}

static long arith_logand(arith_state *st) {
    long v = arith_eq(st);
    while (1) {
        skip_ws(st);
        if (st->p[0] == '&' && st->p[1] == '&') {
            st->p += 2;
            long r = arith_eq(st);
            v = (v && r) ? 1 : 0;
        } else {
            break;
        }
    }
    return v;
}

static long arith_expr(arith_state *st) {
    long v = arith_logand(st);
    while (1) {
        skip_ws(st);
        if (st->p[0] == '|' && st->p[1] == '|') {
            st->p += 2;
            long r = arith_logand(st);
            v = (v || r) ? 1 : 0;
        } else {
            break;
        }
    }
    return v;
}

long msh_arith(const char *expr, const char **err) {
    arith_state st = { .p = expr, .err = NULL };
    long v = arith_expr(&st);
    if (err) *err = st.err;
    return v;
}
