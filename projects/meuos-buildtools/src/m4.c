/*
 * m4.c -- minimal GNU m4-compatible macro processor
 *
 * Compile: cc -O2 -std=c11 -Wall -Wextra -Wpedantic -Werror -o build/m4 src/m4.c
 *
 * Copyright (C) MeuOS Project
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Constants
 * ================================================================ */

#define MAX_MACROS       4096
#define PUSHBACK_INIT    4096
#define MAX_EXPAND_DEPTH  128
#define MAX_INCLUDE_DEPTH  64
#define MAX_LINE          4096
#define MAX_EVAL_DEPTH     64
#define MAX_BACKREFS       10

/* ================================================================
 * Global state
 * ================================================================ */

/* --- Pushback buffer --- */
static char *pb_buf;
static size_t pb_len;
static size_t pb_cap;

/* --- Expansion frames (for user macro expansion / $N handling) --- */
typedef struct {
    const char *text;
    size_t pos;
    const char *name;   /* $0 */
    char **args;        /* $1..$N */
    int nargs;
    int owned_text;     /* 1 if text should be freed on pop */
} ExpFrame;

static ExpFrame *exp_stack;
static int exp_depth;
static int exp_cap;

/* --- Include stack --- */
typedef struct {
    FILE *fp;
    char *name;   /* allocated copy */
    int line;
} IncFrame;

static IncFrame inc_stack[MAX_INCLUDE_DEPTH];
static int inc_depth;

/* --- Command-line file list --- */
static const char **cmdline_files;
static int cmdline_count;
static int cmdline_idx;

/* --- Current base input --- */
static FILE *base_fp;
static char *base_name;   /* allocated copy */
static int base_line;

/* --- State for builtin dispatch (jmp_buf for m4exit) --- */
static jmp_buf exit_env;
static int exit_code;

/* --- Current quote characters --- */
static int qstart_char = '`';
static int qend_char   = '\'';

/* --- Macro table --- */
typedef struct Macro {
    char *name;
    char *value;           /* NULL = builtin */
    const char *bname;     /* builtin name (non-NULL for builtins) */
} Macro;

static Macro macros[MAX_MACROS];
static int nmacros;

/* ================================================================
 * Forward declarations
 * ================================================================ */

static int  next_char(void);
static void pushback(int c);
static void pushback_str(const char *s);

static char *read_name(void);
static char *read_quoted_str(void);

static void builtin_define(int ac, char **av);
static void builtin_undefine(int ac, char **av);
static void builtin_ifdef(int ac, char **av);
static void builtin_ifelse(int ac, char **av);
static void builtin_include(int ac, char **av);
static void builtin_sinclude(int ac, char **av);
static void builtin_substr(int ac, char **av);
static void builtin_translit(int ac, char **av);
static void builtin_len(int ac, char **av);
static void builtin_index(int ac, char **av);
static void builtin_eval(int ac, char **av);
static void builtin_incr(int ac, char **av);
static void builtin_decr(int ac, char **av);
static void builtin_dnl(int ac, char **av);
static void builtin_changequote(int ac, char **av);
static void builtin_m4exit(int ac, char **av);
static void builtin_patsubst(int ac, char **av);
static void builtin_format(int ac, char **av);
static void builtin___file__(int ac, char **av);
static void builtin___line__(int ac, char **av);

static void expand(void);

/* eval expression parser forward decl */
static int eval_expr(int min_prec);

/* ================================================================
 * Helper: safe allocation
 * ================================================================ */

static void *xmalloc(size_t sz)
{
    void *p = malloc(sz ? sz : 1);
    if (!p) { fprintf(stderr, "m4: out of memory\n"); exit(1); }
    return p;
}

static void *xrealloc(void *p, size_t sz)
{
    void *q = realloc(p, sz ? sz : 1);
    if (!q) { fprintf(stderr, "m4: out of memory\n"); exit(1); }
    return q;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *d = xmalloc(n);
    memcpy(d, s, n);
    return d;
}

/* ================================================================
 * Pushback buffer
 * ================================================================ */

static void pb_ensure(size_t n)
{
    while (pb_len + n > pb_cap) {
        pb_cap = pb_cap ? pb_cap * 2 : PUSHBACK_INIT;
        pb_buf = xrealloc(pb_buf, pb_cap);
    }
}

void pushback(int c)
{
    pb_ensure(1);
    pb_buf[pb_len++] = (char)c;
}

void pushback_str(const char *s)
{
    size_t n = strlen(s);
    pb_ensure(n);
    memcpy(pb_buf + pb_len, s, n);
    pb_len += n;
}

/* ================================================================
 * Expansion frame stack
 * ================================================================ */

static void exp_push(const char *text, const char *name,
                     char **args, int nargs, int owned_text)
{
    if (exp_depth >= MAX_EXPAND_DEPTH) {
        fprintf(stderr, "m4: macro expansion nesting too deep\n");
        exit(1);
    }
    if (exp_depth >= exp_cap) {
        exp_cap = exp_cap ? exp_cap * 2 : 16;
        exp_stack = xrealloc(exp_stack, (size_t)exp_cap * sizeof(ExpFrame));
    }
    ExpFrame *f = &exp_stack[exp_depth++];
    f->text = text;
    f->pos = 0;
    f->name = name;
    f->args = args;
    f->nargs = nargs;
    f->owned_text = owned_text;
}

static void exp_pop(void)
{
    if (exp_depth > 0) {
        ExpFrame *f = &exp_stack[exp_depth - 1];
        if (f->args) {
            for (int i = 0; i < f->nargs; i++)
                free(f->args[i]);
            free(f->args);
        }
        if (f->owned_text && f->text)
            free((void *)f->text);
        exp_depth--;
    }
}

/* ================================================================
 * Input system: next_char()
 * ================================================================ */

static int next_char(void)
{
    int c;

    /* 1. pushback buffer */
    if (pb_len > 0)
        return (unsigned char)pb_buf[--pb_len];

    /* 2. expansion frames */
    while (exp_depth > 0) {
        ExpFrame *f = &exp_stack[exp_depth - 1];
        if (f->text[f->pos] == '\0') {
            exp_pop();
            continue;
        }
        c = (unsigned char)f->text[f->pos++];
        /* Handle $N substitution */
        if (c == '$' && f->nargs >= 0) {
            c = (unsigned char)f->text[f->pos++];
            if (c >= '1' && c <= '9') {
                int idx = c - '0';
                if (idx <= f->nargs && f->args[idx - 1]) {
                    pushback_str(f->args[idx - 1]);
                    return next_char();
                }
                return next_char();
            } else if (c == '0') {
                if (f->name) {
                    pushback_str(f->name);
                    return next_char();
                }
                return next_char();
            } else {
                if (c != EOF)
                    pushback(c);
                return '$';
            }
        }
        return c;
    }

    /* 3. include stack */
    while (inc_depth > 0) {
        IncFrame *f = &inc_stack[inc_depth - 1];
        c = fgetc(f->fp);
        if (c != EOF) {
            if (c == '\n')
                f->line++;
            return c;
        }
        fclose(f->fp);
        free(f->name);
        inc_depth--;
    }

    /* 4. base input */
    while (base_fp != NULL) {
        c = fgetc(base_fp);
        if (c != EOF)
            return c;
        fclose(base_fp);
        base_fp = NULL;
        free(base_name);
        base_name = NULL;
        if (cmdline_idx < cmdline_count) {
            const char *fn = cmdline_files[cmdline_idx++];
            base_fp = fopen(fn, "r");
            if (!base_fp) {
                fprintf(stderr, "m4: %s: %s\n", fn, strerror(errno));
                exit(1);
            }
            base_name = xstrdup(fn);
            base_line = 1;
        }
    }

    return EOF;
}

/* ================================================================
 * Get current file/line info
 * ================================================================ */

static const char *current_filename(void)
{
    if (inc_depth > 0)
        return inc_stack[inc_depth - 1].name;
    if (base_name)
        return base_name;
    return "stdin";
}

static int current_lineno(void)
{
    if (inc_depth > 0)
        return inc_stack[inc_depth - 1].line;
    return base_line;
}

/* ================================================================
 * Token / argument reading helpers
 * ================================================================ */

/* Read an identifier [a-zA-Z_][a-zA-Z0-9_]*, return allocated copy. */
static char *read_name(void)
{
    int c = next_char();
    if (c == EOF)
        return NULL;
    if (c != '_' && !isalpha(c)) {
        pushback(c);
        return NULL;
    }
    size_t cap = 64, len = 0;
    char *buf = xmalloc(cap);
    buf[len++] = (char)c;
    while ((c = next_char()) != EOF) {
        if (c != '_' && !isalnum(c)) {
            pushback(c);
            break;
        }
        if (len + 1 >= cap) {
            cap *= 2;
            buf = xrealloc(buf, cap);
        }
        buf[len++] = (char)c;
    }
    buf[len] = '\0';
    return buf;
}

/* Read a quoted string using current quote characters.
 * Returns the content between outermost quotes, allocated.
 * If the next character is not qstart, returns NULL. */
static char *read_quoted_str(void)
{
    int c = next_char();
    if (c != qstart_char) {
        if (c != EOF)
            pushback(c);
        return NULL;
    }
    int depth = 1;
    size_t cap = 256, len = 0;
    char *buf = xmalloc(cap);
    while ((c = next_char()) != EOF) {
        if (c == qstart_char) {
            depth++;
            if (len + 1 >= cap) { cap *= 2; buf = xrealloc(buf, cap); }
            buf[len++] = (char)c;
        } else if (c == qend_char) {
            depth--;
            if (depth == 0)
                break;
            if (len + 1 >= cap) { cap *= 2; buf = xrealloc(buf, cap); }
            buf[len++] = (char)c;
        } else {
            if (len + 1 >= cap) { cap *= 2; buf = xrealloc(buf, cap); }
            buf[len++] = (char)c;
        }
    }
    buf[len] = '\0';
    return buf;
}

/* Strip one layer of outer quotes from s (allocates new string) */
static char *strip_quotes(const char *s)
{
    if (!s)
        return NULL;
    size_t n = strlen(s);
    if (n >= 2 && (unsigned char)s[0] == (unsigned char)qstart_char &&
                  (unsigned char)s[n - 1] == (unsigned char)qend_char) {
        char *r = xmalloc(n - 1);
        memcpy(r, s + 1, n - 2);
        r[n - 2] = '\0';
        return r;
    }
    return xstrdup(s);
}

/* ================================================================
 * Collect macro arguments
 *
 * Starting just after the '(', read characters until the matching ')'.
 * Returns the number of arguments collected and sets *args to an
 * allocated array of allocated strings (each argument is in raw form
 * with any outer quotes intact).
 *
 * Respects quote nesting and parenthesis nesting.
 * ================================================================ */

static int collect_args(char ***args_out)
{
    int max_args = 16, nargs = 0;
    char **args = xmalloc((size_t)max_args * sizeof(char *));

    int paren_depth = 1;   /* we've already consumed the opening '(' */
    int quote_depth = 0;

    size_t cap = 256, len = 0;
    char *cur = xmalloc(cap);
    cur[0] = '\0';

    int c;
    while ((c = next_char()) != EOF) {
        if (c == qstart_char && quote_depth == 0) {
            quote_depth = 1;
            if (len + 1 >= cap) { cap *= 2; cur = xrealloc(cur, cap); }
            cur[len++] = (char)c;
        } else if (c == qend_char && quote_depth == 1) {
            quote_depth = 0;
            if (len + 1 >= cap) { cap *= 2; cur = xrealloc(cur, cap); }
            cur[len++] = (char)c;
        } else if (c == '(' && quote_depth == 0) {
            paren_depth++;
            if (len + 1 >= cap) { cap *= 2; cur = xrealloc(cur, cap); }
            cur[len++] = (char)c;
        } else if (c == ')' && quote_depth == 0) {
            paren_depth--;
            if (paren_depth == 0) {
                cur[len] = '\0';
                char *st = cur, *ed = cur + len;
                while (st < ed && (*st == ' ' || *st == '\t')) st++;
                while (ed > st && (*(ed-1) == ' ' || *(ed-1) == '\t')) ed--;
                if (nargs >= max_args) {
                    max_args *= 2;
                    args = xrealloc(args, (size_t)max_args * sizeof(char *));
                }
                size_t slen = (size_t)(ed - st);
                args[nargs] = xmalloc(slen + 1);
                memcpy(args[nargs], st, slen);
                args[nargs][slen] = '\0';
                nargs++;
                free(cur);
                *args_out = args;
                return nargs;
            }
            if (len + 1 >= cap) { cap *= 2; cur = xrealloc(cur, cap); }
            cur[len++] = (char)c;
        } else if (c == ',' && paren_depth == 1 && quote_depth == 0) {
            cur[len] = '\0';
            char *st = cur, *ed = cur + len;
            while (st < ed && (*st == ' ' || *st == '\t')) st++;
            while (ed > st && (*(ed-1) == ' ' || *(ed-1) == '\t')) ed--;
            if (nargs >= max_args) {
                max_args *= 2;
                args = xrealloc(args, (size_t)max_args * sizeof(char *));
            }
            size_t slen = (size_t)(ed - st);
            args[nargs] = xmalloc(slen + 1);
            memcpy(args[nargs], st, slen);
            args[nargs][slen] = '\0';
            nargs++;
            len = 0;
            cap = 256;
            cur = xrealloc(cur, cap);
            cur[0] = '\0';
        } else {
            if (len + 1 >= cap) { cap *= 2; cur = xrealloc(cur, cap); }
            cur[len++] = (char)c;
        }
    }

    fprintf(stderr, "m4: %s:%d: unexpected EOF in argument list\n",
            current_filename(), current_lineno());
    free(cur);
    *args_out = args;
    return nargs;
}

/* ================================================================
 * Macro table
 * ================================================================ */

static int macro_find_idx(const char *name)
{
    int lo = 0, hi = nmacros - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int cmp = strcmp(macros[mid].name, name);
        if (cmp == 0)
            return mid;
        else if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return lo;
}

static Macro *find_macro(const char *name)
{
    int idx = macro_find_idx(name);
    if (idx < nmacros && strcmp(macros[idx].name, name) == 0)
        return &macros[idx];
    return NULL;
}

static void macro_define(const char *name, const char *value)
{
    int idx = macro_find_idx(name);
    if (idx < nmacros && strcmp(macros[idx].name, name) == 0) {
        free(macros[idx].value);
        macros[idx].value = value ? xstrdup(value) : NULL;
        macros[idx].bname = NULL;
        return;
    }
    if (nmacros >= MAX_MACROS) {
        fprintf(stderr, "m4: macro table overflow\n");
        exit(1);
    }
    if (idx < nmacros)
        memmove(&macros[idx + 1], &macros[idx],
                (size_t)(nmacros - idx) * sizeof(Macro));
    macros[idx].name = xstrdup(name);
    macros[idx].value = value ? xstrdup(value) : NULL;
    macros[idx].bname = NULL;
    nmacros++;
}

static void macro_undefine(const char *name)
{
    int idx = macro_find_idx(name);
    if (idx < nmacros && strcmp(macros[idx].name, name) == 0) {
        free(macros[idx].name);
        free(macros[idx].value);
        nmacros--;
        memmove(&macros[idx], &macros[idx + 1],
                (size_t)(nmacros - idx) * sizeof(Macro));
    }
}

/* Register a builtin */
static void macro_builtin(const char *name)
{
    if (find_macro(name))
        return;
    if (nmacros >= MAX_MACROS) {
        fprintf(stderr, "m4: macro table overflow\n");
        exit(1);
    }
    int idx = macro_find_idx(name);
    if (idx < nmacros)
        memmove(&macros[idx + 1], &macros[idx],
                (size_t)(nmacros - idx) * sizeof(Macro));
    macros[idx].name = xstrdup(name);
    macros[idx].value = NULL;
    macros[idx].bname = name;
    nmacros++;
}

/* Dispatch a builtin by name.
 * Arguments are raw (possibly quoted) strings as collected by collect_args.
 * Each builtin handles quote stripping as needed. */
static void dispatch_builtin(const char *name, int ac, char **av)
{
    if (strcmp(name, "define") == 0)
        builtin_define(ac, av);
    else if (strcmp(name, "undefine") == 0)
        builtin_undefine(ac, av);
    else if (strcmp(name, "ifdef") == 0)
        builtin_ifdef(ac, av);
    else if (strcmp(name, "ifelse") == 0)
        builtin_ifelse(ac, av);
    else if (strcmp(name, "include") == 0)
        builtin_include(ac, av);
    else if (strcmp(name, "sinclude") == 0)
        builtin_sinclude(ac, av);
    else if (strcmp(name, "substr") == 0)
        builtin_substr(ac, av);
    else if (strcmp(name, "translit") == 0)
        builtin_translit(ac, av);
    else if (strcmp(name, "len") == 0)
        builtin_len(ac, av);
    else if (strcmp(name, "index") == 0)
        builtin_index(ac, av);
    else if (strcmp(name, "eval") == 0)
        builtin_eval(ac, av);
    else if (strcmp(name, "incr") == 0)
        builtin_incr(ac, av);
    else if (strcmp(name, "decr") == 0)
        builtin_decr(ac, av);
    else if (strcmp(name, "dnl") == 0)
        builtin_dnl(ac, av);
    else if (strcmp(name, "changequote") == 0)
        builtin_changequote(ac, av);
    else if (strcmp(name, "m4exit") == 0)
        builtin_m4exit(ac, av);
    else if (strcmp(name, "patsubst") == 0)
        builtin_patsubst(ac, av);
    else if (strcmp(name, "format") == 0)
        builtin_format(ac, av);
    else if (strcmp(name, "__file__") == 0)
        builtin___file__(ac, av);
    else if (strcmp(name, "__line__") == 0)
        builtin___line__(ac, av);
    else {
        fprintf(stderr, "m4: internal: unknown builtin '%s'\n", name);
    }
}

/* ================================================================
 * Builtin: define
 * ================================================================ */

static void builtin_define(int ac, char **av)
{
    if (ac < 2) return;
    char *name = strip_quotes(av[0]);
    char *val  = strip_quotes(av[1]);
    if (name && *name)
        macro_define(name, val ? val : "");
    free(name);
    free(val);
}

/* ================================================================
 * Builtin: undefine
 * ================================================================ */

static void builtin_undefine(int ac, char **av)
{
    if (ac < 1) return;
    char *name = strip_quotes(av[0]);
    if (name)
        macro_undefine(name);
    free(name);
}

/* ================================================================
 * Builtin: ifdef
 * ================================================================ */

static void builtin_ifdef(int ac, char **av)
{
    if (ac < 1) return;
    char *name = strip_quotes(av[0]);
    int defined = (name && find_macro(name));
    free(name);
    if (defined) {
        if (ac >= 2)
            printf("%s", av[1]);
    } else {
        if (ac >= 3)
            printf("%s", av[2]);
    }
}

/* ================================================================
 * Builtin: ifelse (multi-branch)
 * ================================================================ */

static void builtin_ifelse(int ac, char **av)
{
    int i = 0;
    while (i + 2 < ac) {  /* need at least 2 args for comparison + 1 for match value */
        char *a = strip_quotes(av[i]);
        char *b = strip_quotes(av[i + 1]);
        int match = (a && b && strcmp(a, b) == 0);
        free(a);
        free(b);
        if (match) {
            if (i + 2 < ac)
                printf("%s", av[i + 2]);
            return;
        }
        i += 3;  /* each pair + its result = 3 args */
    }
    /* remaining arg (if any) is the default */
    if (i < ac)
        printf("%s", av[i]);
}

/* ================================================================
 * Builtin: include / sinclude
 * ================================================================ */

static void include_file(const char *filename, int silent)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        if (!silent) {
            fprintf(stderr, "m4: %s:%d: cannot open '%s': %s\n",
                    current_filename(), current_lineno(), filename, strerror(errno));
        }
        return;
    }
    if (inc_depth >= MAX_INCLUDE_DEPTH) {
        fprintf(stderr, "m4: include nesting too deep\n");
        fclose(fp);
        exit(1);
    }
    IncFrame *f = &inc_stack[inc_depth++];
    f->fp = fp;
    f->name = xstrdup(filename);
    f->line = 1;
}

static void builtin_include(int ac, char **av)
{
    if (ac < 1) return;
    char *fn = strip_quotes(av[0]);
    if (fn)
        include_file(fn, 0);
    free(fn);
}

static void builtin_sinclude(int ac, char **av)
{
    if (ac < 1) return;
    char *fn = strip_quotes(av[0]);
    if (fn)
        include_file(fn, 1);
    free(fn);
}

/* ================================================================
 * Builtin: substr(string, from[, len])
 * ================================================================ */

static void builtin_substr(int ac, char **av)
{
    if (ac < 2) return;
    const char *str = av[0];
    int from = (int)strtol(av[1], NULL, 10);
    int slen = (int)strlen(str);
    if (from < 0) from = 0;
    if (from >= slen) return;
    if (ac >= 3) {
        int n = (int)strtol(av[2], NULL, 10);
        if (n < 0) n = 0;
        int remain = slen - from;
        if (n > remain) n = remain;
        printf("%.*s", n, str + from);
    } else {
        printf("%s", str + from);
    }
}

/* ================================================================
 * Builtin: translit(string, from, to)
 * ================================================================ */

static void builtin_translit(int ac, char **av)
{
    if (ac < 3) return;
    const char *src = av[0];
    const char *from = av[1];
    const char *to = av[2];
    size_t to_len = strlen(to);
    (void)to_len;
    for (const char *p = src; *p; p++) {
        const char *pos = strchr(from, (unsigned char)*p);
        if (pos) {
            size_t idx = (size_t)(pos - from);
            if (idx < to_len)
                putchar((unsigned char)to[idx]);
        } else {
            putchar((unsigned char)*p);
        }
    }
}

/* ================================================================
 * Builtin: len(string)
 * ================================================================ */

static void builtin_len(int ac, char **av)
{
    if (ac < 1) { printf("0"); return; }
    printf("%zu", strlen(av[0]));
}

/* ================================================================
 * Builtin: index(string, substr)
 * ================================================================ */

static void builtin_index(int ac, char **av)
{
    if (ac < 2) { printf("-1"); return; }
    const char *haystack = av[0];
    const char *needle = av[1];
    const char *found = strstr(haystack, needle);
    if (found)
        printf("%td", found - haystack);
    else
        printf("-1");
}

/* ================================================================
 * Builtin: incr / decr
 * ================================================================ */

static void builtin_incr(int ac, char **av)
{
    if (ac < 1) return;
    long v = strtol(av[0], NULL, 10);
    printf("%ld", v + 1);
}

static void builtin_decr(int ac, char **av)
{
    if (ac < 1) return;
    long v = strtol(av[0], NULL, 10);
    printf("%ld", v - 1);
}

/* ================================================================
 * Builtin: eval(expr) — recursive descent integer expression parser
 * ================================================================ */

static const char *eval_src;
static int eval_pos;

static int eval_next(void)
{
    while (eval_src[eval_pos] == ' ' || eval_src[eval_pos] == '\t')
        eval_pos++;
    if (eval_src[eval_pos] == '\0')
        return EOF;
    return (unsigned char)eval_src[eval_pos++];
}

static int eval_peek(void)
{
    int c = eval_next();
    if (c != EOF) eval_pos--;
    return c;
}

static int eval_primary(void)
{
    int c = eval_next();
    if (c == '(') {
        int v = eval_expr(0);
        int r = eval_next();
        if (r != ')') {
            fprintf(stderr, "m4: eval: expected ')'\n");
        }
        return v;
    }
    if (c == '+' || c == '-') {
        int sign = (c == '-') ? -1 : 1;
        int v = eval_primary();
        return sign * v;
    }
    if (c == '~') {
        return ~eval_primary();
    }
    if (c == '!') {
        return !eval_primary();
    }
    if (c >= '0' && c <= '9') {
        int v = c - '0';
        while (1) {
            int n = eval_peek();
            if (n >= '0' && n <= '9') {
                eval_next();
                v = v * 10 + (n - '0');
            } else {
                break;
            }
        }
        return v;
    }
    if (c != EOF)
        eval_pos--;
    return 0;
}

typedef enum { PR_LOGOR, PR_LOGXOR, PR_LOGAND,
               PR_BOR, PR_BXOR, PR_BAND, PR_EQUAL, PR_CMP,
               PR_SHIFT, PR_ADD, PR_MUL } Precedence;

static int eval_expr(int min_prec)
{
    int left = eval_primary();
    while (1) {
        int c = eval_peek();
        int prec = -1;
        int op = 0;

        if (c == '|' && eval_src[eval_pos + 1] == '|') { prec = PR_LOGOR; op = 1; eval_pos += 2; }
        else if (c == '^' && eval_src[eval_pos + 1] == '^') { prec = PR_LOGXOR; op = 2; eval_pos += 2; }
        else if (c == '&' && eval_src[eval_pos + 1] == '&') { prec = PR_LOGAND; op = 3; eval_pos += 2; }
        else if (c == '=' && eval_src[eval_pos + 1] == '=') { prec = PR_EQUAL; op = 7; eval_pos += 2; }
        else if (c == '!' && eval_src[eval_pos + 1] == '=') { prec = PR_EQUAL; op = 8; eval_pos += 2; }
        else if (c == '<' && eval_src[eval_pos + 1] == '<') { prec = PR_SHIFT; op = 13; eval_pos += 2; }
        else if (c == '>' && eval_src[eval_pos + 1] == '>') { prec = PR_SHIFT; op = 14; eval_pos += 2; }
        else if (c == '<' && eval_src[eval_pos + 1] == '=') { prec = PR_CMP; op = 9; eval_pos += 2; }
        else if (c == '>' && eval_src[eval_pos + 1] == '=') { prec = PR_CMP; op = 10; eval_pos += 2; }
        else if (c == '<') { prec = PR_CMP; op = 11; eval_next(); }
        else if (c == '>') { prec = PR_CMP; op = 12; eval_next(); }
        else if (c == '|') { prec = PR_BOR; op = 4; eval_next(); }
        else if (c == '^') { prec = PR_BXOR; op = 5; eval_next(); }
        else if (c == '&') { prec = PR_BAND; op = 6; eval_next(); }
        else if (c == '+') { prec = PR_ADD; op = 15; eval_next(); }
        else if (c == '-') { prec = PR_ADD; op = 16; eval_next(); }
        else if (c == '*') { prec = PR_MUL; op = 17; eval_next(); }
        else if (c == '/') { prec = PR_MUL; op = 18; eval_next(); }
        else if (c == '%') { prec = PR_MUL; op = 19; eval_next(); }

        if (prec < 0 || prec < min_prec)
            break;

        int rhs = eval_expr(prec + 1);
        switch (op) {
            case 1:  left = left || rhs; break;
            case 2:  left = (!left) != (!rhs); break;
            case 3:  left = left && rhs; break;
            case 4:  left = left | rhs; break;
            case 5:  left = left ^ rhs; break;
            case 6:  left = left & rhs; break;
            case 7:  left = left == rhs; break;
            case 8:  left = left != rhs; break;
            case 9:  left = left <= rhs; break;
            case 10: left = left >= rhs; break;
            case 11: left = left < rhs; break;
            case 12: left = left > rhs; break;
            case 13: left = (unsigned)left << (unsigned)rhs; break;
            case 14: left = (unsigned)left >> (unsigned)rhs; break;
            case 15: left = left + rhs; break;
            case 16: left = left - rhs; break;
            case 17: left = left * rhs; break;
            case 18: if (rhs == 0) { fprintf(stderr, "m4: eval: division by zero\n"); rhs = 1; }
                     left = left / rhs; break;
            case 19: if (rhs == 0) { fprintf(stderr, "m4: eval: division by zero\n"); rhs = 1; }
                     left = left % rhs; break;
            default: break;
        }
    }
    return left;
}

static void builtin_eval(int ac, char **av)
{
    if (ac < 1) { printf("0"); return; }
    eval_src = av[0];
    eval_pos = 0;
    int result = eval_expr(0);
    printf("%d", result);
}

/* ================================================================
 * Builtin: dnl — discard to end of line
 * ================================================================ */

static void builtin_dnl(int ac, char **av)
{
    (void)ac;
    (void)av;
    int c;
    while ((c = next_char()) != EOF && c != '\n')
        ;
    if (c == '\n') {
        if (inc_depth > 0)
            inc_stack[inc_depth - 1].line++;
        else
            base_line++;
    }
}

/* ================================================================
 * Builtin: changequote([start[, end]])
 * ================================================================ */

static void builtin_changequote(int ac, char **av)
{
    if (ac < 1) {
        qstart_char = '`';
        qend_char = '\'';
    } else {
        char *s = strip_quotes(av[0]);
        if (s && s[0])
            qstart_char = (unsigned char)s[0];
        free(s);
        if (ac >= 2) {
            char *e = strip_quotes(av[1]);
            if (e && e[0])
                qend_char = (unsigned char)e[0];
            free(e);
        } else {
            qend_char = '\'';
        }
    }
}

/* ================================================================
 * Builtin: m4exit([code])
 * ================================================================ */

static void builtin_m4exit(int ac, char **av)
{
    if (ac >= 1)
        exit_code = (int)strtol(av[0], NULL, 10);
    else
        exit_code = 0;
    longjmp(exit_env, 1);
}

/* ================================================================
 * Builtin: patsubst(string, regex, replacement)
 * ================================================================ */

typedef enum { R_LITERAL, R_DOT, R_STAR, R_STAR_NG, R_OPT,
               R_CLASS_NEG, R_CLASS_POS, R_GROUP, R_GROUP_REF } RNodeType;

typedef struct RNode {
    RNodeType type;
    unsigned char ch;
    unsigned char *chars;
    int nchars;
    struct RNode *child;
    struct RNode *next;
    int ref_idx;
} RNode;

static void rnode_free(RNode *n)
{
    if (!n) return;
    free(n->chars);
    rnode_free(n->child);
    rnode_free(n->next);
    free(n);
}

static RNode *rnode_alloc(RNodeType t)
{
    RNode *n = xmalloc(sizeof(RNode));
    memset(n, 0, sizeof(RNode));
    n->type = t;
    return n;
}

static const char *rparse(const char *p, RNode **out);

static const char *rparse_alt(const char *p, RNode **out)
{
    RNode *head = NULL, **tail = &head;
    while (*p) {
        RNode *n = NULL;
        /* Groups use POSIX basic style \(...\), so a plain ) is literal */
        if (*p == '\\' && *(p+1) == ')')
            break;
        p = rparse(p, &n);
        if (!n) break;
        *tail = n;
        tail = &n->next;
    }
    *out = head;
    return p;
}

static const char *rparse(const char *p, RNode **out)
{
    if (!*p) { *out = NULL; return p; }

    RNode *n = NULL;

    if (*p == '\\' && *(p+1) == '(') {
        /* Group: \(...\) */
        n = rnode_alloc(R_GROUP);
        p = rparse_alt(p + 2, &n->child);
        if (*p == '\\' && *(p+1) == ')')
            p += 2;
    } else if (*p == '\\' && *(p+1) >= '1' && *(p+1) <= '9') {
        n = rnode_alloc(R_GROUP_REF);
        n->ref_idx = (int)(*(p+1) - '0');
        p += 2;
    } else if (*p == '\\') {
        n = rnode_alloc(R_LITERAL);
        n->ch = (unsigned char)*(p+1);
        p += 2;
    } else if (*p == '.') {
        n = rnode_alloc(R_DOT);
        p++;
    } else if (*p == '[') {
        p++;
        int neg = 0;
        if (*p == '^') { neg = 1; p++; }
        /* Use a 256-byte bitmap for fast matching */
        unsigned char cls[256];
        memset(cls, 0, sizeof(cls));
        while (*p && *p != ']') {
            unsigned char c1 = (unsigned char)*p++;
            if (*p == '-' && *(p+1) && *(p+1) != ']') {
                /* Range: c1 - next */
                p++;
                unsigned char c2 = (unsigned char)*p++;
                if (c1 <= c2) {
                    for (unsigned char r = c1; r <= c2; r++)
                        cls[r] = 1;
                } else {
                    for (unsigned char r = c2; r <= c1; r++)
                        cls[r] = 1;
                }
            } else {
                cls[c1] = 1;
            }
        }
        if (*p == ']') p++;
        n = rnode_alloc(neg ? R_CLASS_NEG : R_CLASS_POS);
        n->chars = xmalloc(256);
        memcpy(n->chars, cls, 256);
        n->nchars = 256;
    } else if (*p == '*') {    } else {
        n = rnode_alloc(R_LITERAL);
        n->ch = (unsigned char)*p++;
    }

    if (*p == '*') {
        RNode *q = NULL;
        if (*(p+1) == '?') {
            q = rnode_alloc(R_STAR_NG);
            p += 2;
        } else {
            q = rnode_alloc(R_STAR);
            p++;
        }
        q->child = n;
        n = q;
    } else if (*p == '?') {
        RNode *q = rnode_alloc(R_OPT);
        q->child = n;
        n = q;
        p++;
    }

    *out = n;
    return p;
}

static RNode *rparse_regex(const char *pat)
{
    RNode *result = NULL;
    rparse_alt(pat, &result);
    return result;
}

typedef struct {
    int start;
    int end;
} RGroup;

static int rmatch(RNode *n, const char *s, int pos, int len,
                  RGroup *groups, int *ngroups);

static int rmatch_seq(RNode *n, const char *s, int pos, int len,
                      RGroup *groups, int *ngroups)
{
    for (RNode *c = n; c; c = c->next) {
        int consumed = rmatch(c, s, pos, len, groups, ngroups);
        if (consumed < 0) return -1;
        pos += consumed;
    }
    return pos;
}

static int rmatch(RNode *n, const char *s, int pos, int len,
                  RGroup *groups, int *ngroups)
{
    if (!n) return 0;
    if (pos > len) return -1;

    switch (n->type) {
    case R_LITERAL:
        if (pos < len && (unsigned char)s[pos] == n->ch) return 1;
        return -1;

    case R_DOT:
        if (pos < len) return 1;
        return -1;

    case R_CLASS_POS: {
        if (pos >= len) return -1;
        unsigned char c = (unsigned char)s[pos];
        if (n->chars[c]) return 1;
        return -1;
    }

    case R_CLASS_NEG: {
        if (pos >= len) return -1;
        unsigned char c = (unsigned char)s[pos];
        if (n->chars[c]) return -1;
        return 1;
    }

    case R_STAR: {
        int max = len - pos;
        int cpos = pos;
        while (cpos < len && (cpos - pos) < max) {
            int saved_ng = *ngroups;
            int consumed = rmatch(n->child, s, cpos, len, groups, ngroups);
            if (consumed <= 0) break;
            *ngroups = saved_ng;
            cpos += consumed;
        }
        max = cpos - pos;
        for (int m = max; m >= 0; m--) {
            int new_pos = pos + m;
            int saved_ng = *ngroups;
            int rest = rmatch_seq(n->next, s, new_pos, len, groups, ngroups);
            if (rest >= 0)
                return m;
            *ngroups = saved_ng;
        }
        return -1;
    }

    case R_STAR_NG: {
        for (int m = 0; m <= len - pos; m++) {
            int rest = rmatch_seq(n->next, s, pos + m, len, groups, ngroups);
            if (rest >= 0) return m;
            if (pos + m >= len) break;
            int saved_ng = *ngroups;
            int consumed = rmatch(n->child, s, pos + m, len, groups, ngroups);
            *ngroups = saved_ng;
            if (consumed <= 0) break;
        }
        return -1;
    }

    case R_OPT: {
        int saved_ng = *ngroups;
        int rest;
        int consumed = rmatch(n->child, s, pos, len, groups, ngroups);
        if (consumed >= 0) {
            int new_pos = pos + consumed;
            rest = rmatch_seq(n->next, s, new_pos, len, groups, ngroups);
            if (rest >= 0) {
                *ngroups = saved_ng;
                return consumed;
            }
        }
        *ngroups = saved_ng;
        rest = rmatch_seq(n->next, s, pos, len, groups, ngroups);
        if (rest >= 0) return 0;
        return -1;
    }

    case R_GROUP: {
        int gid = *ngroups;
        if (gid < MAX_BACKREFS) {
            groups[gid].start = pos;
            *ngroups = gid + 1;
        }
        int saved_ng = *ngroups;
        int consumed = rmatch_seq(n->child, s, pos, len, groups, ngroups);
        if (consumed < 0) {
            *ngroups = saved_ng;
            return -1;
        }
        int new_pos = consumed;
        if (gid < MAX_BACKREFS)
            groups[gid].end = new_pos;
        return new_pos - pos;
    }

    case R_GROUP_REF: {
        if (n->ref_idx <= 0 || n->ref_idx > MAX_BACKREFS) return -1;
        RGroup *g = &groups[n->ref_idx - 1];
        if (g->start < 0 || g->end < g->start) return -1;
        int glen = g->end - g->start;
        if (pos + glen > len) return -1;
        if (memcmp(s + pos, s + g->start, (size_t)glen) == 0)
            return glen;
        return -1;
    }

    default:
        return -1;
    }
}

typedef struct {
    int start;
    int end;
    RGroup groups[MAX_BACKREFS];
    int ngroups;
} RMatch;

static int rmatch_find(RNode *re, const char *s, int len, int start_pos, RMatch *m)
{
    for (int p = start_pos; p <= len; p++) {
        RGroup groups[MAX_BACKREFS];
        for (int i = 0; i < MAX_BACKREFS; i++) {
            groups[i].start = -1;
            groups[i].end = -1;
        }
        int ng = 0;
        int consumed = rmatch_seq(re, s, p, len, groups, &ng);
        if (consumed >= 0) {
            m->start = p;
            m->end = consumed;
            m->ngroups = ng;
            for (int i = 0; i < ng && i < MAX_BACKREFS; i++)
                m->groups[i] = groups[i];
            return 1;
        }
    }
    return 0;
}

static void builtin_patsubst(int ac, char **av)
{
    if (ac < 3) return;
    const char *str = av[0];
    const char *pat = av[1];
    const char *rep = av[2];
    int slen = (int)strlen(str);

    RNode *re = rparse_regex(pat);
    if (!re) {
        printf("%s", str);
        return;
    }

    int pos = 0;
    while (pos <= slen) {
        RMatch m;
        if (rmatch_find(re, str, slen, pos, &m)) {
            printf("%.*s", m.start - pos, str + pos);
            for (const char *r = rep; *r; r++) {
                if (*r == '\\') {
                    r++;
                    if (*r == '&') {
                        printf("%.*s", m.end - m.start, str + m.start);
                    } else if (*r >= '1' && *r <= '9') {
                        int idx = *r - '0';
                        if (idx <= m.ngroups && m.groups[idx - 1].start >= 0) {
                            int gs = m.groups[idx - 1].start;
                            int ge = m.groups[idx - 1].end;
                            printf("%.*s", ge - gs, str + gs);
                        }
                    } else if (*r) {
                        putchar((unsigned char)*r);
                    }
                } else {
                    putchar((unsigned char)*r);
                }
            }
            pos = m.end;
            if (pos == m.start)
                pos++;
        } else {
            printf("%s", str + pos);
            break;
        }
    }

    rnode_free(re);
}

/* ================================================================
 * Builtin: format(fmt, ...)
 * ================================================================ */

static void builtin_format(int ac, char **av)
{
    if (ac < 1) return;
    const char *fmt = av[0];
    int argi = 1;

    for (const char *p = fmt; *p; p++) {
        if (*p == '%') {
            p++;
            int width = 0;
            int zero_pad = 0;
            int left_just = 0;
            while (*p == '0' || *p == '-') {
                if (*p == '0') zero_pad = 1;
                if (*p == '-') left_just = 1;
                p++;
            }
            while (*p >= '0' && *p <= '9') {
                width = width * 10 + (*p - '0');
                p++;
            }
            if (!*p) break;
            char buf[128];
            switch (*p) {
            case 's':
                if (argi <= ac) {
                    const char *val = av[argi++];
                    if (width > 0) {
                        int vlen = (int)strlen(val);
                        int pad = width - vlen;
                        char pc = zero_pad ? '0' : ' ';
                        if (left_just) {
                            printf("%s", val);
                            for (int i = 0; i < pad; i++) putchar(pc);
                        } else {
                            for (int i = 0; i < pad; i++) putchar(pc);
                            printf("%s", val);
                        }
                    } else {
                        printf("%s", val);
                    }
                }
                break;
            case 'd': {
                if (argi <= ac) {
                    long v = strtol(av[argi++], NULL, 10);
                    int n = snprintf(buf, sizeof(buf), "%ld", v);
                    if (width > n) {
                        int pad = width - n;
                        if (left_just) {
                            printf("%s", buf);
                            for (int i = 0; i < pad; i++) putchar(' ');
                        } else {
                            char pc = zero_pad ? '0' : ' ';
                            if (v < 0 && zero_pad) {
                                putchar('-');
                                for (int i = 0; i < pad; i++) putchar('0');
                                printf("%s", buf + 1);
                            } else {
                                for (int i = 0; i < pad; i++) putchar(pc);
                                printf("%s", buf);
                            }
                        }
                    } else {
                        printf("%s", buf);
                    }
                }
                break;
            }
            case 'x':
            case 'X': {
                if (argi <= ac) {
                    unsigned long v = (unsigned long)strtol(av[argi++], NULL, 10);
                    int n = snprintf(buf, sizeof(buf), (*p == 'X') ? "%lX" : "%lx", v);
                    if (width > n) {
                        int pad = width - n;
                        char pc = zero_pad ? '0' : ' ';
                        if (left_just) {
                            printf("%s", buf);
                            for (int i = 0; i < pad; i++) putchar(pc);
                        } else {
                            for (int i = 0; i < pad; i++) putchar(pc);
                            printf("%s", buf);
                        }
                    } else {
                        printf("%s", buf);
                    }
                }
                break;
            }
            case 'c': {
                if (argi <= ac) {
                    long v = strtol(av[argi++], NULL, 10);
                    putchar((int)v);
                }
                break;
            }
            case '%':
                putchar('%');
                break;
            default:
                putchar('%');
                putchar((unsigned char)*p);
                break;
            }
        } else {
            putchar((unsigned char)*p);
        }
    }
}

/* ================================================================
 * Builtin: __file__ / __line__
 * ================================================================ */

static void builtin___file__(int ac, char **av)
{
    (void)ac;
    (void)av;
    printf("%s", current_filename());
}

static void builtin___line__(int ac, char **av)
{
    (void)ac;
    (void)av;
    printf("%d", current_lineno());
}

/* ================================================================
 * Main expansion loop
 *
 * NOTE: The peek for '(' after a macro name must NOT use the global
 * pushback buffer, because pushbacked chars get processed before
 * expansion frames, causing interleaving between main-input chars
 * and macro-expansion text. Instead, peeked chars that are not part
 * of a macro call are pushed as an "owned text" expansion frame
 * below the macro's own expansion, so they are re-read at the right time.
 * ================================================================ */

static void expand(void)
{
    /* Helper to count lines on output */
        int c;
        while ((c = next_char()) != EOF) {
            if (c == qstart_char) {
                char *qs = read_quoted_str();
                if (qs) {
                    printf("%s", qs);
                    /* Count newlines in output */
                    for (const char *p = qs; *p; p++) {
                        if (*p == '\n') {
                            if (inc_depth > 0) inc_stack[inc_depth - 1].line++;
                            else base_line++;
                        }
                    }
                    free(qs);
                }
            } else if (c == '_' || isalpha(c)) {
            pushback(c);
            char *name = read_name();
            if (!name) continue;

            Macro *m = find_macro(name);

            /*
             * For MACROS: skip spaces/tabs and peek for '(' to decide
             * whether this is a call with arguments. The peeked chars
             * (spaces + following non-space) are saved locally and
             * replayed as an expansion frame after the macro expansion.
             *
             * For NON-macros: only peek ONE character without skipping
             * spaces, so we don't consume chars belonging to the next word.
             */
            int has_paren = 0;
            char peekbuf[64];
            int npeek = 0;
            int peek_end = EOF;

            if (m) {
                /* Macro — skip spaces, peek for '(' */
                while (npeek < (int)(sizeof(peekbuf) - 2)) {
                    int p = next_char();
                    if (p == ' ' || p == '\t') {
                        peekbuf[npeek++] = (char)p;
                    } else {
                        peek_end = p;
                        break;
                    }
                }
            } else {
                /* Non-macro — peek just one char */
                peek_end = next_char();
            }

            if (peek_end == '(') {
                has_paren = 1;
            }

            if (m && m->bname) {
                /* --- Builtin macro --- */
                int is_dnl = (strcmp(name, "dnl") == 0);

                if (has_paren) {
                    char **args = NULL;
                    int nargs = collect_args(&args);
                    if (!is_dnl && args) {
                        for (int i = 0; i < nargs; i++) {
                            char *stripped = strip_quotes(args[i]);
                            free(args[i]);
                            args[i] = stripped;
                        }
                    }
                    if (is_dnl) {
                        if (args) { for (int i = 0; i < nargs; i++) free(args[i]); free(args); }
                        builtin_dnl(0, NULL);
                        npeek = 0;
                    } else {
                        dispatch_builtin(name, nargs, args);
                        if (args) { for (int i = 0; i < nargs; i++) free(args[i]); free(args); }
                    }
                } else {
                    if (is_dnl) {
                        builtin_dnl(0, NULL);
                        npeek = 0;
                    } else {
                        /* Builtin called without args but with no '(' */
                        dispatch_builtin(name, 0, NULL);
                    }
                }

                /* Restore peeked chars (but not for dnl, which consumed its line) */
                if (!has_paren && !is_dnl) {
                    if (peek_end != EOF) {
                        peekbuf[npeek++] = (char)peek_end;
                        peekbuf[npeek] = '\0';
                        exp_push(xstrdup(peekbuf), NULL, NULL, -1, 1);
                    } else if (npeek > 0) {
                        peekbuf[npeek] = '\0';
                        exp_push(xstrdup(peekbuf), NULL, NULL, -1, 1);
                    }
                }
            } else if (m && m->value) {
                /* --- User macro --- */
                if (!has_paren) {
                    /* Push peek text FIRST (bottom of stack, read second) */
                    if (peek_end != EOF) {
                        peekbuf[npeek++] = (char)peek_end;
                        peekbuf[npeek] = '\0';
                        exp_push(xstrdup(peekbuf), NULL, NULL, -1, 1);
                    } else if (npeek > 0) {
                        peekbuf[npeek] = '\0';
                        exp_push(xstrdup(peekbuf), NULL, NULL, -1, 1);
                    }
                }
                if (has_paren) {
                    char **args = NULL;
                    int nargs = collect_args(&args);
                    exp_push(m->value, m->name, args, nargs, 0);
                } else {
                    exp_push(m->value, m->name, NULL, 0, 0);
                }
            } else {
                /* --- Not a macro — output name and peeked char(s) --- */
                printf("%s", name);
                if (has_paren) {
                    char **args = NULL;
                    int nargs = collect_args(&args);
                    putchar('(');
                    for (int i = 0; i < nargs; i++) {
                        if (i > 0) putchar(',');
                        printf("%s", args[i] ? args[i] : "");
                    }
                    putchar(')');
                    if (args) { for (int i = 0; i < nargs; i++) free(args[i]); free(args); }
                } else {
                    if (npeek > 0) {
                        for (int i = 0; i < npeek; i++) {
                            if (peekbuf[i] == '\n') {
                                if (inc_depth > 0) inc_stack[inc_depth - 1].line++;
                                else base_line++;
                            }
                        }
                        fwrite(peekbuf, 1, (size_t)npeek, stdout);
                    }
                    if (peek_end != EOF) {
                        if (peek_end == '\n') {
                            if (inc_depth > 0) inc_stack[inc_depth - 1].line++;
                            else base_line++;
                        }
                        putchar(peek_end);
                    }
                }
            }

            free(name);
        } else {
            if (c == '\n') {
                if (inc_depth > 0) inc_stack[inc_depth - 1].line++;
                else base_line++;
            }
            putchar(c);
        }
    }
}

/* ================================================================
 * Initialize builtins
 * ================================================================ */

static void init_builtins(void)
{
    macro_builtin("changequote");
    macro_builtin("__file__");
    macro_builtin("__line__");
    macro_builtin("decr");
    macro_builtin("define");
    macro_builtin("dnl");
    macro_builtin("eval");
    macro_builtin("format");
    macro_builtin("ifdef");
    macro_builtin("ifelse");
    macro_builtin("incr");
    macro_builtin("include");
    macro_builtin("index");
    macro_builtin("len");
    macro_builtin("m4exit");
    macro_builtin("patsubst");
    macro_builtin("sinclude");
    macro_builtin("substr");
    macro_builtin("translit");
    macro_builtin("undefine");
}

/* ================================================================
 * main
 * ================================================================ */

int m4_main(int argc, char **argv)
{
    /* Init pushback buffer */
    pb_buf = NULL;
    pb_len = 0;
    pb_cap = 0;

    /* Init expansion stack */
    exp_stack = NULL;
    exp_depth = 0;
    exp_cap = 0;

    /* Init macro table */
    nmacros = 0;
    init_builtins();

    /* Setup setjmp for m4exit */
    exit_code = 0;
    if (setjmp(exit_env)) {
        /* m4exit was called — clean up */
        free(pb_buf);
        while (exp_depth > 0) {
            ExpFrame *f = &exp_stack[exp_depth - 1];
            if (f->args) {
                for (int i = 0; i < f->nargs; i++)
                    free(f->args[i]);
                free(f->args);
            }
            if (f->owned_text && f->text)
                free((void *)f->text);
            exp_depth--;
        }
        free(exp_stack);
        while (inc_depth > 0) {
            IncFrame *f = &inc_stack[inc_depth - 1];
            fclose(f->fp);
            free(f->name);
            inc_depth--;
        }
        free(base_name);
        for (int i = 0; i < nmacros; i++) {
            free(macros[i].name);
            free(macros[i].value);
        }
        return exit_code;
    }

    /* Setup input */
    if (argc > 1) {
        cmdline_files = (const char **)(argv + 1);
        cmdline_count = argc - 1;
        cmdline_idx = 0;
        const char *fn = cmdline_files[cmdline_idx++];
        base_fp = fopen(fn, "r");
        if (!base_fp) {
            fprintf(stderr, "m4: %s: %s\n", fn, strerror(errno));
            return 1;
        }
        base_name = xstrdup(fn);
    } else {
        base_fp = stdin;
        base_name = xstrdup("stdin");
        cmdline_files = NULL;
        cmdline_count = 0;
        cmdline_idx = 0;
    }
    base_line = 1;

    expand();

    /* Cleanup */
    free(pb_buf);
    while (exp_depth > 0) {
        ExpFrame *f = &exp_stack[exp_depth - 1];
        if (f->args) {
            for (int i = 0; i < f->nargs; i++)
                free(f->args[i]);
            free(f->args);
        }
        if (f->owned_text && f->text)
            free((void *)f->text);
        exp_depth--;
    }
    free(exp_stack);
    while (inc_depth > 0) {
        IncFrame *f = &inc_stack[inc_depth - 1];
        fclose(f->fp);
        free(f->name);
        inc_depth--;
    }
    if (base_fp && base_fp != stdin)
        fclose(base_fp);
    free(base_name);
    for (int i = 0; i < nmacros; i++) {
        free(macros[i].name);
        free(macros[i].value);
    }

    return exit_code;
}
