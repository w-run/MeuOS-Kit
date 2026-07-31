/* libutils/syntax.c — 轻量语法着色
 *
 * 不做完整解析；基于 token 分类（关键字/字符串/数字/注释）。
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "meuos/color.h"
#include "meuos/syntax.h"
#include "meuos/utils.h"

syn_language_t syn_detect(const char *path) {
    if (!path) return SYN_LANG_UNKNOWN;
    const char *ext = strrchr(path, '.');
    if (!ext) {
        /* 特殊文件名 */
        if (strcmp(path, "Makefile") == 0) return SYN_LANG_SHELL;
        return SYN_LANG_UNKNOWN;
    }
    if (strcasecmp(ext, ".c") == 0 || strcasecmp(ext, ".h") == 0) return SYN_LANG_C;
    if (strcasecmp(ext, ".py") == 0) return SYN_LANG_PYTHON;
    if (strcasecmp(ext, ".sh") == 0) return SYN_LANG_SHELL;
    if (strcasecmp(ext, ".bash") == 0) return SYN_LANG_SHELL;
    if (strcasecmp(ext, ".rs") == 0) return SYN_LANG_RUST;
    if (strcasecmp(ext, ".go") == 0) return SYN_LANG_GO;
    if (strcasecmp(ext, ".js") == 0) return SYN_LANG_JS;
    if (strcasecmp(ext, ".ts") == 0) return SYN_LANG_JS;
    if (strcasecmp(ext, ".json") == 0) return SYN_LANG_JSON;
    if (strcasecmp(ext, ".yaml") == 0 || strcasecmp(ext, ".yml") == 0) return SYN_LANG_YAML;
    if (strcasecmp(ext, ".md") == 0) return SYN_LANG_MARKDOWN;
    if (strcasecmp(ext, ".diff") == 0 || strcasecmp(ext, ".patch") == 0) return SYN_LANG_DIFF;
    return SYN_LANG_UNKNOWN;
}

/* 关键字集合（C 子集） */
static int is_c_keyword(const char *tok, size_t len) {
    static const char *kw[] = {
        "if", "else", "while", "for", "do", "return", "break", "continue",
        "switch", "case", "default", "sizeof", "typedef", "struct", "union",
        "enum", "static", "extern", "const", "volatile", "register", "auto",
        "signed", "unsigned", "char", "short", "int", "long", "float", "double",
        "void", "inline", "goto", "NULL",
        NULL
    };
    for (int i = 0; kw[i]; i++) {
        if (strlen(kw[i]) == len && memcmp(tok, kw[i], len) == 0) return 1;
    }
    return 0;
}

static int is_shell_keyword(const char *tok, size_t len) {
    static const char *kw[] = {
        "if", "then", "else", "elif", "fi", "case", "esac", "for", "while",
        "until", "do", "done", "function", "return", "in", "select",
        "export", "local", "readonly", "declare", "unset",
        NULL
    };
    for (int i = 0; kw[i]; i++) {
        if (strlen(kw[i]) == len && memcmp(tok, kw[i], len) == 0) return 1;
    }
    return 0;
}

static int is_python_keyword(const char *tok, size_t len) {
    static const char *kw[] = {
        "def", "class", "if", "elif", "else", "while", "for", "in", "is",
        "and", "or", "not", "return", "yield", "import", "from", "as",
        "pass", "break", "continue", "try", "except", "finally", "raise",
        "with", "lambda", "None", "True", "False",
        NULL
    };
    for (int i = 0; kw[i]; i++) {
        if (strlen(kw[i]) == len && memcmp(tok, kw[i], len) == 0) return 1;
    }
    return 0;
}

typedef enum {
    TOK_NORMAL,
    TOK_WORD,
    TOK_STRING,
    TOK_COMMENT,
    TOK_NUMBER,
    TOK_KEYWORD,
} tok_class_t;

static tok_class_t classify_c(syn_language_t lang, const char *p, const char **end) {
    const char *start = p;
    unsigned char c = (unsigned char)*p;
    if (c == '/' && p[1] == '/') {
        *end = start + strlen(p);  /* 行尾 */
        return TOK_COMMENT;
    }
    if (c == '/' && p[1] == '*') {
        const char *e = strstr(p + 2, "*/");
        *end = e ? e + 2 : start + strlen(p);
        return TOK_COMMENT;
    }
    if (c == '"' || c == '\'') {
        char q = c;
        const char *e = p + 1;
        while (*e && *e != q) {
            if (*e == '\\') e++;
            e++;
        }
        if (*e) e++;
        *end = e;
        return TOK_STRING;
    }
    if (c == '_' || isalpha(c)) {
        const char *e = p;
        while (*e && (isalnum((unsigned char)*e) || *e == '_')) e++;
        *end = e;
        /* 检查关键字 */
        if (lang == SYN_LANG_C && is_c_keyword(p, (size_t)(e - p))) return TOK_KEYWORD;
        return TOK_WORD;
    }
    if (isdigit(c)) {
        const char *e = p;
        while (*e && (isalnum((unsigned char)*e) || *e == '.' || *e == 'x')) e++;
        *end = e;
        return TOK_NUMBER;
    }
    *end = p + 1;
    return TOK_NORMAL;
}

static tok_class_t classify_shell(const char *p, const char **end) {
    unsigned char c = (unsigned char)*p;
    if (c == '#') {
        *end = p + strlen(p);
        return TOK_COMMENT;
    }
    if (c == '"') {
        const char *e = p + 1;
        while (*e && *e != '"') {
            if (*e == '\\') e++;
            e++;
        }
        if (*e) e++;
        *end = e;
        return TOK_STRING;
    }
    if (c == '\'') {
        const char *e = p + 1;
        while (*e && *e != '\'') e++;
        if (*e) e++;
        *end = e;
        return TOK_STRING;
    }
    if (c == '$' && (p[1] == '(' || p[1] == '{')) {
        char closer = p[1] == '(' ? ')' : '}';
        const char *e = strchr(p + 2, closer);
        *end = e ? e + 1 : p + strlen(p);
        return TOK_STRING;
    }
    if (c == '_' || isalpha(c)) {
        const char *e = p;
        while (*e && (isalnum((unsigned char)*e) || *e == '_')) e++;
        *end = e;
        if (is_shell_keyword(p, (size_t)(e - p))) return TOK_KEYWORD;
        return TOK_WORD;
    }
    if (isdigit(c)) {
        const char *e = p;
        while (*e && (isalnum((unsigned char)*e) || *e == '.')) e++;
        *end = e;
        return TOK_NUMBER;
    }
    *end = p + 1;
    return TOK_NORMAL;
}

static tok_class_t classify_python(const char *p, const char **end) {
    unsigned char c = (unsigned char)*p;
    if (c == '#') {
        *end = p + strlen(p);
        return TOK_COMMENT;
    }
    if (c == '"' || c == '\'') {
        int triple = (p[1] == c && p[2] == c);
        char q = c;
        const char *e;
        if (triple) {
            e = strstr(p + 3, ">>>" + 0);  /* placeholder */
            char triple_q[4] = {q, q, q, 0};
            e = strstr(p + 3, triple_q);
            *end = e ? e + 3 : p + strlen(p);
        } else {
            e = p + 1;
            while (*e && *e != q) {
                if (*e == '\\') e++;
                e++;
            }
            if (*e) e++;
            *end = e;
        }
        return TOK_STRING;
    }
    if (c == '_' || isalpha(c)) {
        const char *e = p;
        while (*e && (isalnum((unsigned char)*e) || *e == '_')) e++;
        *end = e;
        if (is_python_keyword(p, (size_t)(e - p))) return TOK_KEYWORD;
        return TOK_WORD;
    }
    if (isdigit(c)) {
        const char *e = p;
        while (*e && (isalnum((unsigned char)*e) || *e == '.')) e++;
        *end = e;
        return TOK_NUMBER;
    }
    *end = p + 1;
    return TOK_NORMAL;
}

static tok_class_t classify_diff(const char *p, const char **end) {
    unsigned char c = (unsigned char)*p;
    if (c == '+') { *end = p + 1; return TOK_STRING; }
    if (c == '-') { *end = p + 1; return TOK_COMMENT; }
    if (c == '@') { *end = p + 1; return TOK_KEYWORD; }
    *end = p + 1;
    return TOK_NORMAL;
}

void syntax_highlight_line(syn_language_t lang, const char *line,
                           size_t len, FILE *outfp, int color) {
    const char *p = line;
    const char *end = line + len;
    while (p < end) {
        const char *e;
        tok_class_t k;
        switch (lang) {
        case SYN_LANG_C:
            k = classify_c(lang, p, &e); break;
        case SYN_LANG_PYTHON:
            k = classify_python(p, &e); break;
        case SYN_LANG_DIFF:
            k = classify_diff(p, &e); break;
        default:
            k = classify_shell(p, &e); break;
        }
        size_t n = (size_t)(e - p);
        if (color && color_enabled >= 0) {
            const char *cstr = "";
            switch (k) {
            case TOK_KEYWORD: cstr = color_named(5);  /* 紫：关键字 */ break;
            case TOK_STRING:  cstr = color_named(2);  /* 绿：字符串 */ break;
            case TOK_NUMBER:  cstr = color_named(3);  /* 黄：数字 */ break;
            case TOK_COMMENT: cstr = color_named(8);  /* 灰：注释 */ break;
            case TOK_WORD:    cstr = color_named(6);  /* 青：标识符 */ break;
            default:          cstr = ""; break;
            }
            fputs(cstr, outfp);
            fwrite(p, 1, n, outfp);
            if (cstr[0]) fputs(color_reset(), outfp);
        } else {
            fwrite(p, 1, n, outfp);
        }
        p = e;
    }
}
