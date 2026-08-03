/* msh/var/expand.c - 字符串展开核心
 *
 * 支持的修饰符见 include/msh/expand.h。
 *
 * 命令替换 $(...) 通过 fork+pipe 递归调用 msh_eval 实现，
 * 子进程 stdout 经管道回收到父进程的 buf。
 *
 * tilde 仅处理独立 ~ 替换 $HOME，~user 不支持。
 */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "msh/exec.h"
#include "msh/expand.h"
#include "msh/lex.h"
#include "msh/parse.h"
#include "msh/array.h"

/* === 动态字符串 === */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} sbuf_t;

static void sbuf_init(sbuf_t *s) {
    s->cap = 32;
    s->len = 0;
    s->buf = malloc(s->cap);
    s->buf[0] = '\0';
}

static void sbuf_push(sbuf_t *s, const char *p, size_t n) {
    while (s->len + n + 1 >= s->cap) {
        s->cap *= 2;
        s->buf = realloc(s->buf, s->cap);
    }
    memcpy(s->buf + s->len, p, n);
    s->len += n;
    s->buf[s->len] = '\0';
}

static void sbuf_pushc(sbuf_t *s, char c) { sbuf_push(s, &c, 1); }
static void sbuf_pushs(sbuf_t *s, const char *str) {
    if (str) sbuf_push(s, str, strlen(str));
}

static char *sbuf_steal(sbuf_t *s) { return s->buf; }

/* === 变量取值（与 parse.c 原 get_var 等价，独立实现避免互相依赖） === */
static const char *get_var(const char *name) {
    if (!name || !*name) return NULL;
    if (isdigit((unsigned char)name[0])) {
        int n = atoi(name);
        if (n == 0) return msh_program_name;
        char vn[16];
        snprintf(vn, sizeof(vn), "%d", n);
        const char *v = getenv(vn);
        if (v) return v;
        if (n >= 1 && n < 64 && msh_argv && msh_argv[n]) return msh_argv[n];
        return NULL;
    }
    if (!strcmp(name, "#")) {
        const char *v = getenv("#");
        return v ? v : "0";
    }
    if (!strcmp(name, "?")) {
        static char buf[32];
        snprintf(buf, sizeof(buf), "%d", msh_last_status);
        return buf;
    }
    if (!strcmp(name, "$")) {
        static char buf[32];
        snprintf(buf, sizeof(buf), "%d", (int)getpid());
        return buf;
    }
    if (!strcmp(name, "!")) {
        /* 最后后台作业 PID（简化：返回 0 表示无） */
        return "0";
    }
    if (!strcmp(name, "@") || !strcmp(name, "*")) {
        static char buf[4096];
        const char *cnt = getenv("#");
        int n = cnt ? atoi(cnt) : 0;
        size_t len = 0;
        for (int i = 1; i <= n; i++) {
            char vn[16];
            snprintf(vn, sizeof(vn), "%d", i);
            const char *v = getenv(vn);
            if (!v) continue;
            if (len > 0 && len + 1 < sizeof(buf)) buf[len++] = ' ';
            size_t vl = strlen(v);
            if (len + vl + 1 >= sizeof(buf)) vl = sizeof(buf) - len - 1;
            memcpy(buf + len, v, vl);
            len += vl;
        }
        buf[len] = '\0';
        return buf;
    }
    return getenv(name);
}

/* === 命令替换 $(...) / `...` === */
char *msh_cmdsub(const char *cmd) {
    int p[2];
    if (pipe(p) < 0) { perror("[cmdsub] pipe"); return strdup(""); }
    pid_t pid = fork();
    if (pid < 0) { perror("[cmdsub] fork"); close(p[0]); close(p[1]); return strdup(""); }
    if (pid == 0) {
        /* child: stdout -> p[1] */
        dup2(p[1], 1);
        close(p[0]);
        close(p[1]);
        lexer_t lx;
        msh_lexer_init(&lx, cmd, strlen(cmd));
        ast_t *ast = msh_parse(&lx);
        if (ast) {
            int rc = msh_eval(ast);
            msh_last_status = rc;
            ast_free(ast);
        }
        msh_lexer_free(&lx);
        _exit(msh_last_status);
    }
    /* parent: read p[0] */
    close(p[1]);
    sbuf_t out;
    sbuf_init(&out);
    char buf[4096];
    ssize_t n;
    while ((n = read(p[0], buf, sizeof(buf))) > 0) {
        sbuf_push(&out, buf, (size_t)n);
    }
    close(p[0]);
    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) msh_last_status = WEXITSTATUS(status);
    /* 去尾换行 */
    while (out.len > 0 && (out.buf[out.len - 1] == '\n' || out.buf[out.len - 1] == '\r')) {
        out.buf[--out.len] = '\0';
    }
    return sbuf_steal(&out);
}

/* === ${...} 内部解析（已消费 `${`，停在 `}`）=== */
/* 在 body 中查找 `${VAR<prefix>...}` 的修饰符起点。
 * 跳过 VAR 名（alphanum/_，或 # 长度）。返回前缀符号位置索引。*/
static long find_modifier_pos(const char *body) {
    long i = 0;
    if (body[0] == '#') return 1;  /* ${#VAR} 长度 */
    while (body[i] && (isalnum((unsigned char)body[i]) || body[i] == '_')) i++;
    return i;
}

/* fnmatch 模式匹配 + 剥离前缀/后缀。
 * mode: 1 = #  最短前缀   2 = ## 最长前缀
 *       3 = %  最短后缀   4 = %% 最长后缀
 * 前缀剥离：val[0..i) 匹配 pat -> 返回 val[i..]
 * 后缀剥离：val[i..vlen) 匹配 pat -> 返回 val[0..i)
 *   %  最短后缀 = 保留最长前缀 = i 从 vlen 递减到 0，首个命中即返回
 *   %% 最长后缀 = 保留最短前缀 = i 从 0 递增到 vlen，首个命中即返回 */
static char *strip_pattern(const char *val, const char *pat, int mode) {
    size_t vlen = strlen(val);
    if (mode == 1) {
        for (size_t i = 0; i <= vlen; i++) {
            char *tmp = strndup(val, i);
            int m = fnmatch(pat, tmp, 0);
            free(tmp);
            if (m == 0) return strdup(val + i);
        }
        return strdup(val);
    }
    if (mode == 2) {
        for (size_t i = vlen + 1; i-- > 0; ) {
            char *tmp = strndup(val, i);
            int m = fnmatch(pat, tmp, 0);
            free(tmp);
            if (m == 0) return strdup(val + i);
        }
        return strdup(val);
    }
    if (mode == 3) {
        for (size_t i = vlen + 1; i-- > 0; ) {
            if (fnmatch(pat, val + i, 0) == 0) return strndup(val, i);
        }
        return strdup(val);
    }
    /* mode == 4 */
    for (size_t i = 0; i <= vlen; i++) {
        if (fnmatch(pat, val + i, 0) == 0) return strndup(val, i);
    }
    return strdup(val);
}

/* ${VAR/pat/repl}：首次替换 */
static char *subst_first(const char *val, const char *pat, const char *repl) {
    size_t vlen = strlen(val);
    /* 找到最早匹配的位置 i，使得 val[i..j] 匹配 pat */
    for (size_t i = 0; i <= vlen; i++) {
        for (size_t j = vlen; j >= i; j--) {
            char *tmp = strndup(val + i, j - i);
            int m = fnmatch(pat, tmp, 0);
            free(tmp);
            if (m == 0) {
                char *out = malloc(i + (repl ? strlen(repl) : 0) + (vlen - j) + 1);
                memcpy(out, val, i);
                if (repl) memcpy(out + i, repl, strlen(repl));
                memcpy(out + i + (repl ? strlen(repl) : 0), val + j, vlen - j);
                out[i + (repl ? strlen(repl) : 0) + (vlen - j)] = '\0';
                return out;
            }
            if (i == j) break;  /* 避免 size_t 下溢 */
        }
    }
    return strdup(val);
}

/* 处理 ${...} 修饰符 */
static void handle_brace(sbuf_t *out, const char *body) {
    /* bash array: ${arr[@]} ${arr[*]} ${arr[N]} ${#arr[@]} */
    {
        /* Check for ${#arr[@]} - array count */
        if (body[0] == '#' && body[1] != '\0') {
            const char *lb = strchr(body + 1, '[');
            if (lb && strcmp(lb, "[@]") == 0) {
                char aname[128];
                size_t nl = lb - body - 1;
                if (nl >= sizeof(aname)) nl = sizeof(aname) - 1;
                memcpy(aname, body + 1, nl);
                aname[nl] = '\0';
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", msh_array_count(aname));
                sbuf_pushs(out, buf);
                return;
            }
        }
        /* Check for ${arr[...]} - array access */
        const char *lb = strchr(body, '[');
        if (lb && lb != body) {
            const char *rb = strrchr(body, ']');
            if (rb && rb > lb) {
                char aname[128];
                size_t nl = lb - body;
                if (nl >= sizeof(aname)) nl = sizeof(aname) - 1;
                memcpy(aname, body, nl);
                aname[nl] = '\0';
                
                /* Content inside [] */
                size_t ilen = rb - lb - 1;
                if (strcmp(lb, "[@]") == 0 || strcmp(lb, "[*]") == 0) {
                    /* All elements */
                    const char *sep = (lb[1] == '@') ? " " : " ";
                    char *all = msh_array_get_all(aname, sep);
                    sbuf_pushs(out, all);
                    free(all);
                    return;
                } else {
                    /* Specific index: arr[N] */
                    char idxbuf[32];
                    if (ilen >= sizeof(idxbuf)) ilen = sizeof(idxbuf) - 1;
                    memcpy(idxbuf, lb + 1, ilen);
                    idxbuf[ilen] = '\0';
                    int idx = atoi(idxbuf);
                    const char *elem = msh_array_get(aname, idx);
                    if (elem) sbuf_pushs(out, elem);
                    return;
                }
            }
        }
    }
    /* 长度 ${#VAR} */
    if (body[0] == '#' && body[1] != '\0') {
        const char *v = get_var(body + 1);
        char buf[32];
        snprintf(buf, sizeof(buf), "%zu", v ? strlen(v) : 0);
        sbuf_pushs(out, buf);
        return;
    }
    /* VAR 名 */
    long mpos = find_modifier_pos(body);
    char varname[128];
    size_t vl = (size_t)mpos;
    if (vl >= sizeof(varname)) vl = sizeof(varname) - 1;
    memcpy(varname, body, vl);
    varname[vl] = '\0';
    const char *mod = body + mpos;
    const char *val = get_var(varname);

    if (*mod == '\0') {
        sbuf_pushs(out, val);
        return;
    }
    /* :-  :=  :+  :?  四种 */
    if (mod[0] == ':' && (mod[1] == '-' || mod[1] == '=' || mod[1] == '+' || mod[1] == '?')) {
        const char *arg = mod + 2;
        int unset = (!val || !*val);
        switch (mod[1]) {
        case '-':
            if (unset) sbuf_pushs(out, arg);
            else sbuf_pushs(out, val);
            return;
        case '=':
            if (unset) {
                setenv(varname, arg, 1);
                sbuf_pushs(out, arg);
            } else {
                sbuf_pushs(out, val);
            }
            return;
        case '+':
            if (!unset) sbuf_pushs(out, arg);
            return;
        case '?':
            if (unset) {
                fprintf(stderr, "msh: %s: %s\n", varname, *arg ? arg : "parameter null or not set");
                exit(1);
            }
            sbuf_pushs(out, val);
            return;
        }
    }
    /* 不带冒号的 - = + ?（仅未设时触发） */
    if (mod[0] == '-' || mod[0] == '=' || mod[0] == '+' || mod[0] == '?') {
        const char *arg = mod + 1;
        int unset = (!val);
        switch (mod[0]) {
        case '-':
            if (unset) sbuf_pushs(out, arg);
            else sbuf_pushs(out, val);
            return;
        case '=':
            if (unset) { setenv(varname, arg, 1); sbuf_pushs(out, arg); }
            else sbuf_pushs(out, val);
            return;
        case '+':
            if (!unset) sbuf_pushs(out, arg);
            return;
        case '?':
            if (unset) {
                fprintf(stderr, "msh: %s: %s\n", varname, *arg ? arg : "parameter null or not set");
                exit(1);
            }
            sbuf_pushs(out, val);
            return;
        }
    }
    /* 前缀/后缀剥离 # ## % %% */
    if (mod[0] == '#' || mod[0] == '%' || mod[0] == '/') {
        const char *pat = mod + 1;
        if (mod[0] == '#') {
            int mode = (mod[1] == '#') ? 2 : 1;
            if (mode == 2) pat = mod + 2;
            char *r = strip_pattern(val ? val : "", pat, mode);
            sbuf_pushs(out, r);
            free(r);
            return;
        }
        if (mod[0] == '%') {
            int mode = (mod[1] == '%') ? 4 : 3;
            if (mode == 4) pat = mod + 2;
            char *r = strip_pattern(val ? val : "", pat, mode);
            sbuf_pushs(out, r);
            free(r);
            return;
        }
        if (mod[0] == '/') {
            /* ${VAR/pat/repl}：首次替换 */
            char *pat_copy = strdup(pat);
            char *repl = strchr(pat_copy, '/');
            if (repl) *repl++ = '\0';
            char *r = subst_first(val ? val : "", pat_copy, repl ? repl : "");
            sbuf_pushs(out, r);
            free(r);
            free(pat_copy);
            return;
        }
    }
    /* 默认：当作 ${VAR} */
    sbuf_pushs(out, val);
}

char *msh_expand(const char *s) {
    if (!s) return NULL;
    sbuf_t out;
    sbuf_init(&out);
    size_t len = strlen(s);
    size_t i = 0;
    while (i < len) {
        char c = s[i];
        if (c == '$' && i + 1 < len) {
            char n = s[i + 1];
            if (n == '{') {
                /* ${...}：扫描配对 } */
                size_t start = i + 2;
                size_t j = start;
                int depth = 1;
                while (j < len && depth > 0) {
                    if (s[j] == '{') depth++;
                    else if (s[j] == '}') { depth--; if (depth == 0) break; }
                    j++;
                }
                size_t body_len = j - start;
                char *body = malloc(body_len + 1);
                memcpy(body, s + start, body_len);
                body[body_len] = '\0';
                handle_brace(&out, body);
                free(body);
                i = (j < len) ? j + 1 : j;
                continue;
            }
            if (n == '(') {
                /* $(...) 或 $((...)) */
                if (i + 2 < len && s[i + 2] == '(') {
                    /* 算术 $((...)) */
                    size_t j = i + 3;
                    int depth = 1;
                    while (j < len && depth > 0) {
                        if (s[j] == '(') depth++;
                        else if (s[j] == ')') depth--;
                        if (depth == 0) break;
                        j++;
                    }
                    /* j 指向第一个匹配的 )；后面还应跟一个 ) */
                    char *raw = strndup(s + i + 3, j - (i + 3));
                    /* 先展开表达式内的 $VAR/$(...) 再求值（bash 行为） */
                    char *expr = msh_expand(raw);
                    free(raw);
                    const char *err = NULL;
                    long v = msh_arith(expr, &err);
                    free(expr);
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%ld", v);
                    sbuf_pushs(&out, buf);
                    /* 跳过 )) */
                    i = j + 1;
                    if (i < len && s[i] == ')') i++;
                    continue;
                }
                /* 命令替换 $(...) */
                size_t j = i + 2;
                int depth = 1;
                while (j < len && depth > 0) {
                    if (s[j] == '(') depth++;
                    else if (s[j] == ')') { depth--; if (depth == 0) break; }
                    j++;
                }
                char *raw = strndup(s + i + 2, j - (i + 2));
                /* 命令文本先做参数展开（bash 行为：$(cmd $var) 里 $var 先展开） */
                char *cmd = msh_expand(raw);
                free(raw);
                char *r = msh_cmdsub(cmd);
                sbuf_pushs(&out, r);
                free(r);
                free(cmd);
                i = (j < len) ? j + 1 : j;
                continue;
            }
            if (isalpha((unsigned char)n) || n == '_') {
                size_t j = i + 1;
                while (j < len && (isalnum((unsigned char)s[j]) || s[j] == '_')) j++;
                char *var = strndup(s + i + 1, j - i - 1);
                const char *v = get_var(var);
                sbuf_pushs(&out, v);
                free(var);
                i = j;
                continue;
            }
            if (isdigit((unsigned char)n) || n == '@' || n == '*' || n == '#' || n == '?' || n == '$' || n == '!') {
                char nm[2] = { n, 0 };
                const char *v = get_var(nm);
                sbuf_pushs(&out, v);
                i += 2;
                continue;
            }
            /* 单独 $ */
            sbuf_pushc(&out, '$');
            i++;
            continue;
        }
        if (c == '`') {
            /* 反引号命令替换 */
            size_t j = i + 1;
            while (j < len && s[j] != '`') {
                if (s[j] == '\\' && j + 1 < len) j += 2;
                else j++;
            }
            char *raw = strndup(s + i + 1, j - i - 1);
            char *cmd = msh_expand(raw);
            free(raw);
            char *r = msh_cmdsub(cmd);
            sbuf_pushs(&out, r);
            free(r);
            free(cmd);
            i = (j < len) ? j + 1 : j;
            continue;
        }
        sbuf_pushc(&out, c);
        i++;
    }
    return sbuf_steal(&out);
}
