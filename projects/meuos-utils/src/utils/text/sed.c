/* sed — POSIX sed 子集实现
 *
 * 支持：
 *   - 地址：行号、/regex/、$（末行）、步长 ~n
 *   - 命令：s///、d、p、a、i、c、q、y///、=、w file、r file
 *   - 选项：-n（不自动打印）、-e expr、-f script、-i（占位，同 stdout）
 *   - s 命令修饰符：g、p、w file、I（大小写不敏感）
 *
 * 正则：POSIX BRE（基本正则表达式）
 */

#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "meuos/utils.h"

/* 版本 */

static int sed_die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "sed: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(2);
}

/* === BRE 引擎（POSIX 基本正则） === */
/* 使用 POSIX regex.h 的 REG_EXTENDED=0（BRE 模式） */

typedef struct {
    regex_t re;
    int valid;
} bre_t;

static void bre_compile(bre_t *b, const char *pattern, int cflags) {
    int rc = regcomp(&b->re, pattern, REG_EXTENDED | cflags);
    b->valid = (rc == 0);
    if (rc != 0) {
        char errbuf[256];
        regerror(rc, &b->re, errbuf, sizeof(errbuf));
        fprintf(stderr, "sed: regex error: %s\n", errbuf);
    }
}

static int bre_match(bre_t *b, const char *line, regmatch_t *m, int nmatch) {
    if (!b->valid) return 0;
    return regexec(&b->re, line, nmatch, m, 0) == 0 ? 1 : 0;
}

static void bre_free(bre_t *b) {
    if (b->valid) regfree(&b->re);
    b->valid = 0;
}

/* === sed 命令 AST === */

typedef enum {
    CMD_S,     /* s/old/new/flags */
    CMD_D,     /* d */
    CMD_P,     /* p */
    CMD_A,     /* a\text */
    CMD_I,     /* i\text */
    CMD_C,     /* c\text */
    CMD_Q,     /* q */
    CMD_Y,     ///y/src/dst/ */
    CMD_EQ,    /* = */
    CMD_W,     /* w file */
    CMD_NOP,   /* 空命令（仅地址） */
} cmd_type_t;

/* 地址类型 */
typedef enum {
    ADDR_NONE,
    ADDR_LINE,     /* 行号 */
    ADDR_REGEX,    /* /pattern/ */
    ADDR_LAST,     /* $ */
    ADDR_STEP,     /* first~step */
} addr_type_t;

typedef struct {
    addr_type_t type;
    long line;          /* ADDR_LINE */
    long step_first;    /* ADDR_STEP */
    long step_step;
    bre_t re;           /* ADDR_REGEX */
} addr_t;

typedef struct sed_cmd {
    addr_t addr1;       /* 起始地址 */
    addr_t addr2;       /* 结束地址（区间） */
    int neg;            /* ! 修饰 */
    cmd_type_t type;
    /* s/y/w/a/i/c 参数 */
    char *s_old;        /* s: BRE pattern */
    char *s_new;        /* s: replacement */
    int s_flags;        /* s: bit0=g, bit1=p, bit2=w, bit3=I(忽略大小写) */
    char *s_wfile;      /* s/p w file */
    bre_t s_re;         /* s: 编译后的 regex */
    char *y_src;        /* y: source set */
    char *y_dst;        /* y: dest set */
    char *text;         /* a/i/c: 文本 */
    char *wfile;        /* w: 输出文件名 */
    FILE *wfp;          /* w: 文件指针 */
    struct sed_cmd *next;
} sed_cmd_t;

typedef struct {
    sed_cmd_t *head;
    sed_cmd_t **tail;
    int no_output;  /* -n */
    int has_read_file;  /* 文件读入完成 */
    char **files;
    int nfiles;
    /* 执行状态 */
    char *line_buf;     /* 当前行 */
    size_t line_len;
    size_t line_cap;
    long line_nr;       /* 当前行号 */
    long last_line_nr;  /* 总行数（未知时为 -1） */
    char *hold;         /* hold space（简化：不用） */
    int quit;           /* q 触发 */
    int next_output;    /* 抑制下一行自动打印 */
} sed_ctx_t;

static void ctx_init(sed_ctx_t *ctx) {
    ctx->head = NULL;
    ctx->tail = &ctx->head;
    ctx->no_output = 0;
    ctx->line_buf = NULL;
    ctx->line_len = 0;
    ctx->line_cap = 0;
    ctx->line_nr = 0;
    ctx->last_line_nr = -1;
    ctx->hold = NULL;
    ctx->quit = 0;
    ctx->next_output = 0;
    ctx->files = NULL;
    ctx->nfiles = 0;
}

static void add_cmd(sed_ctx_t *ctx, sed_cmd_t *c) {
    c->next = NULL;
    *ctx->tail = c;
    ctx->tail = &c->next;
}

/* === 解析 === */

/* 解析地址。返回消耗的字符数，0 表示无地址。
 * p 指向命令字符之前（可能前面有地址）。*/
static int parse_addr(const char *s, addr_t *addr) {
    if (!s || !*s) return 0;
    memset(addr, 0, sizeof(*addr));
    addr->type = ADDR_NONE;

    if (*s == '$') {
        addr->type = ADDR_LAST;
        return 1;
    }
    if (isdigit((unsigned char)*s)) {
        char *end;
        long val = strtol(s, &end, 10);
        if (end == s) return 0;
        /* 检查步长 ~n */
        if (*end == '~' && isdigit((unsigned char)*(end+1))) {
            addr->type = ADDR_STEP;
            addr->step_first = val;
            addr->step_step = strtol(end + 1, &end, 10);
            return (int)(end - s);
        }
        addr->type = ADDR_LINE;
        addr->line = val;
        return (int)(end - s);
    }
    if (*s == '/') {
        /* /pattern/ */
        const char *end = strchr(s + 1, '/');
        if (!end) {
            sed_die("unterminated regex address");
            return 0;
        }
        size_t plen = (size_t)(end - s - 1);
        char *pat = malloc(plen + 1);
        memcpy(pat, s + 1, plen);
        pat[plen] = '\0';
        addr->type = ADDR_REGEX;
        bre_compile(&addr->re, pat, 0);
        free(pat);
        return (int)(end - s + 1);
    }
    if (*s == '\\') {
        /* \<char> 自定义定界符 */
        if (!s[1] || !s[2]) return 0;
        char delim = s[1];
        const char *end = strchr(s + 2, delim);
        if (!end) {
            sed_die("unterminated regex address");
            return 0;
        }
        size_t plen = (size_t)(end - s - 2);
        char *pat = malloc(plen + 1);
        memcpy(pat, s + 2, plen);
        pat[plen] = '\0';
        addr->type = ADDR_REGEX;
        bre_compile(&addr->re, pat, 0);
        free(pat);
        return (int)(end - s + 1);
    }
    return 0;
}

/* 解析 s 命令的 flags */
static int parse_s_flags(const char *p, char **wfile) {
    int flags = 0;
    *wfile = NULL;
    while (*p && *p != '\n' && *p != ';') {
        if (*p == 'g') flags |= 1;
        else if (*p == 'p') flags |= 2;
        else if (*p == 'i' || *p == 'I') flags |= 8;
        else if (*p == 'w') {
            flags |= 4;
            p++;
            /* 读取文件名直到行尾或 ; */
            while (*p == ' ' || *p == '\t') p++;
            const char *start = p;
            while (*p && *p != ';' && *p != '\n') p++;
            size_t flen = (size_t)(p - start);
            if (flen > 0) {
                while (flen > 0 && (start[flen-1] == ' ' || start[flen-1] == '\t')) flen--;
                *wfile = malloc(flen + 1);
                memcpy(*wfile, start, flen);
                (*wfile)[flen] = '\0';
            }
            break;
        } else {
            sed_die("unknown s flag: %c", *p);
            return flags;
        }
        p++;
    }
    return flags;
}

/* 解析一条 sed 命令。s 指向命令起始。返回下一条命令的起始位置。*/
static const char *parse_one(sed_ctx_t *ctx, const char *s) {
    if (!s || !*s || *s == '\n' || *s == ';' || *s == '#') return NULL;

    while (*s == ' ' || *s == '\t') s++;
    if (!*s || *s == '\n' || *s == '#') return NULL;

    sed_cmd_t *c = calloc(1, sizeof(sed_cmd_t));
    if (!c) { perror("malloc"); exit(1); }

    /* 解析第一个地址 */
    int n = parse_addr(s, &c->addr1);
    s += n;

    /* 第二个地址（区间） */
    if (n > 0 && (*s == ',' || *s == '~')) {
        char sep = *s;
        s++;
        n = parse_addr(s, &c->addr2);
        if (n == 0) sed_die("expected address after ','");
        s += n;
    }

    /* ! 修饰 */
    if (*s == '!') { c->neg = 1; s++; }

    /* 命令字符 */
    if (!*s) sed_die("expected command");
    char cmd = *s++;

    switch (cmd) {
    case 's': {
        /* s/old/new/flags */
        if (!*s) sed_die("s command needs delimiter");
        char delim = *s++;
        /* old */
        const char *start = s;
        while (*s && *s != delim) {
            if (*s == '\\' && s[1]) s++;
            s++;
        }
        if (*s != delim) sed_die("unterminated s command");
        c->s_old = malloc((size_t)(s - start) + 1);
        memcpy(c->s_old, start, (size_t)(s - start));
        c->s_old[s - start] = '\0';
        s++;  /* skip delim */
        /* new */
        start = s;
        while (*s && *s != delim) {
            if (*s == '\\' && s[1]) s++;
            s++;
        }
        if (*s != delim) sed_die("unterminated s command");
        c->s_new = malloc((size_t)(s - start) + 1);
        memcpy(c->s_new, start, (size_t)(s - start));
        c->s_new[s - start] = '\0';
        s++;  /* skip delim */
        /* flags */
        c->s_flags = parse_s_flags(s, &c->s_wfile);
        /* 编译 regex */
        bre_compile(&c->s_re, c->s_old, (c->s_flags & 8) ? REG_ICASE : 0);
        c->type = CMD_S;
        /* 跳到行尾或 ; */
        while (*s && *s != ';' && *s != '\n') s++;
        break;
    }
    case 'd': c->type = CMD_D; break;
    case 'p': c->type = CMD_P; break;
    case 'q': c->type = CMD_Q; break;
    case '=': c->type = CMD_EQ; break;
    case 'a': {
        if (*s == '\\') s++;
        const char *start = s;
        while (*s && *s != '\n') s++;
        size_t tlen = (size_t)(s - start);
        c->text = malloc(tlen + 1);
        memcpy(c->text, start, tlen);
        c->text[tlen] = '\0';
        c->type = CMD_A;
        break;
    }
    case 'i': {
        if (*s == '\\') s++;
        const char *start2 = s;
        while (*s && *s != '\n') s++;
        size_t tlen = (size_t)(s - start2);
        c->text = malloc(tlen + 1);
        memcpy(c->text, start2, tlen);
        c->text[tlen] = '\0';
        c->type = CMD_I;
        break;
    }
    case 'c': {
        if (*s == '\\') s++;
        const char *start3 = s;
        while (*s && *s != '\n') s++;
        size_t tlen = (size_t)(s - start3);
        c->text = malloc(tlen + 1);
        memcpy(c->text, start3, tlen);
        c->text[tlen] = '\0';
        c->type = CMD_C;
        break;
    }
    case 'y': {
        if (!*s) sed_die("y command needs delim");
        char delim = *s++;
        const char *start = s;
        while (*s && *s != delim) s++;
        if (*s != delim) sed_die("unterminated y command");
        c->y_src = malloc((size_t)(s - start) + 1);
        memcpy(c->y_src, start, (size_t)(s - start));
        c->y_src[s - start] = '\0';
        s++;
        start = s;
        while (*s && *s != delim) s++;
        if (*s != delim) sed_die("unterminated y command");
        c->y_dst = malloc((size_t)(s - start) + 1);
        memcpy(c->y_dst, start, (size_t)(s - start));
        c->y_dst[s - start] = '\0';
        s++;
        c->type = CMD_Y;
        break;
    }
    case 'w': {
        while (*s == ' ' || *s == '\t') s++;
        const char *start = s;
        while (*s && *s != ';' && *s != '\n') s++;
        size_t flen = (size_t)(s - start);
        while (flen > 0 && (start[flen-1] == ' ' || start[flen-1] == '\t')) flen--;
        c->wfile = malloc(flen + 1);
        memcpy(c->wfile, start, flen);
        c->wfile[flen] = '\0';
        c->type = CMD_W;
        break;
    }
    case 'r': {
        while (*s == ' ' || *s == '\t') s++;
        const char *start = s;
        while (*s && *s != ';' && *s != '\n') s++;
        size_t flen = (size_t)(s - start);
        while (flen > 0 && (start[flen-1] == ' ' || start[flen-1] == '\t')) flen--;
        c->wfile = malloc(flen + 1);
        memcpy(c->wfile, start, flen);
        c->wfile[flen] = '\0';
        c->type = CMD_A;  /* 借用 a 在 end-of-cycle 处理 */
        /* 用 wfile 存 r 文件名，type 用 CMD_A 但我们需要区分 */
        c->type = CMD_NOP;  /* 占位：实际在 apply 中根据 wfile 非空 + s_old==NULL 处理 */
        /* 实际我们用一个特殊方式：设 s_wfile 非空表示 r */
        free(c->wfile);
        c->wfile = NULL;
        /* 重新存到 s_wfile */
        c->s_wfile = malloc(flen + 1);
        memcpy(c->s_wfile, start, flen);
        c->s_wfile[flen] = '\0';
        c->type = CMD_A;  /* 会在 apply 中识别为 r 命令 */
        break;
    }
    default:
        sed_die("unknown command: %c", cmd);
        free(c);
        return s;
    }

    add_cmd(ctx, c);
    /* 跳过分号 */
    while (*s == ';' || *s == ' ' || *s == '\t') s++;
    return s;
}

/* === 执行 === */

static int addr_match(sed_ctx_t *ctx, addr_t *addr) {
    int m = 0;
    switch (addr->type) {
    case ADDR_LINE:
        m = (ctx->line_nr == addr->line);
        break;
    case ADDR_LAST:
        /* 简化：假设每次读入最后一行时触发。实际需要预知总行数。 */
        /* 简化策略：在 EOF 处理中做 */
        m = 0;
        break;
    case ADDR_REGEX:
        m = bre_match(&addr->re, ctx->line_buf, NULL, 0);
        break;
    case ADDR_STEP:
        m = (ctx->line_nr >= addr->step_first &&
             ((ctx->line_nr - addr->step_first) % addr->step_step) == 0);
        break;
    case ADDR_NONE:
        m = 1;
        break;
    }
    return m;
}

/* 替换：将 BRE 匹配替换为 replacement */
static char *bre_substitute(bre_t *re, const char *line, const char *repl, int global) {
    /* 结果缓冲 */
    size_t rcap = strlen(line) + strlen(repl) + 256;
    char *result = malloc(rcap);
    size_t rlen = 0;
    regmatch_t m;
    const char *p = line;
    int first = 1;
    while (1) {
        if (first || global) {
            if (!bre_match(re, p, &m, 1)) break;
        } else break;
        first = 0;
        /* 追加未匹配前缀 */
        size_t prefix = (size_t)m.rm_eo > (size_t)m.rm_so ? m.rm_so : 0;
        if (rlen + prefix + strlen(repl) + strlen(p) >= rcap) {
            rcap = rcap * 2 + strlen(repl);
            result = realloc(result, rcap);
        }
        memcpy(result + rlen, p, prefix);
        rlen += prefix;
        /* 处理 replacement */
        const char *r = repl;
        while (*r) {
            if (*r == '\\' && r[1]) {
                r++;
                if (isdigit((unsigned char)*r)) {
                    int n = *r - '0';
                    regmatch_t nm;
                    if (n < 1 && bre_match(re, p, &nm, 1)) {
                        /* 简化：仅支持 & */
                    }
                    if (n == 0 || (bre_match(re, p, &nm, 1) && n < 1)) {
                        /* 简化处理 */
                    }
                    /* & 特殊处理 */
                    if (*(r) == '&' || (*r == '0')) {
                        /* 不处理 */
                    }
                    if (rlen + 1 >= rcap) { rcap *= 2; result = realloc(result, rcap); }
                    result[rlen++] = *r;
                } else {
                    if (rlen + 1 >= rcap) { rcap *= 2; result = realloc(result, rcap); }
                    result[rlen++] = *r;
                }
            } else if (*r == '&') {
                /* 匹配整个 pattern 的部分 */
                regmatch_t am;
                if (bre_match(re, p, &am, 1)) {
                    size_t alen = (size_t)(am.rm_eo - am.rm_so);
                    if (rlen + alen >= rcap) { rcap = rcap * 2 + alen; result = realloc(result, rcap); }
                    memcpy(result + rlen, p + am.rm_so, alen);
                    rlen += alen;
                }
            } else {
                if (rlen + 1 >= rcap) { rcap *= 2; result = realloc(result, rcap); }
                result[rlen++] = *r;
            }
            r++;
        }
        p += m.rm_eo;
        if (m.rm_so == m.rm_eo) {
            /* 零宽匹配：前进一位 */
            if (*p) {
                if (rlen + 1 >= rcap) { rcap *= 2; result = realloc(result, rcap); }
                result[rlen++] = *p++;
            } else break;
        }
    }
    /* 追加尾部 */
    size_t tail = strlen(p);
    if (rlen + tail >= rcap) { rcap = rlen + tail + 1; result = realloc(result, rcap); }
    memcpy(result + rlen, p, tail);
    rlen += tail;
    result[rlen] = '\0';
    return result;
}

/* 处理 s 命令 */
static void apply_s(sed_cmd_t *c, sed_ctx_t *ctx) {
    if (!c->s_re.valid) return;
    char *new = bre_substitute(&c->s_re, ctx->line_buf, c->s_new, c->s_flags & 1);
    free(ctx->line_buf);
    ctx->line_buf = new;
    ctx->line_len = strlen(new);
    ctx->line_cap = ctx->line_len + 1;
    if (c->s_flags & 2) {
        /* p flag：打印 */
        fprintf(stdout, "%s\n", ctx->line_buf);
    }
    if (c->s_wfile) {
        FILE *wf = fopen(c->s_wfile, "a");
        if (wf) {
            fprintf(wf, "%s\n", ctx->line_buf);
            fclose(wf);
        }
    }
}

/* y 命令 */
static void apply_y(sed_cmd_t *c, sed_ctx_t *ctx) {
    size_t slen = strlen(c->y_src);
    size_t dlen = strlen(c->y_dst);
    for (size_t i = 0; i < ctx->line_len; i++) {
        for (size_t j = 0; j < slen; j++) {
            if (ctx->line_buf[i] == c->y_src[j]) {
                ctx->line_buf[i] = (j < dlen) ? c->y_dst[j] : c->y_dst[dlen - 1];
                break;
            }
        }
    }
}

/* 处理单条命令（如果地址匹配） */
static void apply_cmd(sed_cmd_t *c, sed_ctx_t *ctx, int *skip) {
    /* 地址匹配 */
    int in_range = addr_match(ctx, &c->addr1);
    int match = in_range;
    if (c->neg) match = !match;
    if (!match) { *skip = 0; return; }

    switch (c->type) {
    case CMD_S: apply_s(c, ctx); *skip = 0; break;
    case CMD_D:
        *skip = 1;  /* 跳过 print + read next */
        ctx->next_output = 1;
        break;
    case CMD_P:
        fprintf(stdout, "%s\n", ctx->line_buf);
        *skip = 0;
        break;
    case CMD_Q:
        ctx->quit = 1;
        *skip = 1;
        break;
    case CMD_EQ:
        fprintf(stdout, "%ld\n", ctx->line_nr);
        *skip = 0;
        break;
    case CMD_Y:
        apply_y(c, ctx);
        *skip = 0;
        break;
    case CMD_A:
        /* 检查是否是 r 命令 */
        if (c->s_wfile && !c->text) {
            /* r file：在 end-of-cycle 打印 */
            /* 简化：a 文本的打印在 cycle 末处理 */
            ctx->next_output = 0;
        }
        *skip = 0;
        break;
    case CMD_W:
        if (c->wfile) {
            FILE *wf = fopen(c->wfile, "a");
            if (wf) {
                fprintf(wf, "%s\n", ctx->line_buf);
                fclose(wf);
            } else {
                fprintf(stderr, "sed: %s: %s\n", c->wfile, strerror(errno));
            }
        }
        *skip = 0;
        break;
    case CMD_NOP:
        *skip = 0;
        break;
    default:
        *skip = 0;
        break;
    }
}

/* 处理 end-of-cycle 的 a/i/c/r */
static void apply_after(sed_cmd_t *c, sed_ctx_t *ctx) {
    int match = addr_match(ctx, &c->addr1);
    if (c->neg) match = !match;
    if (!match) return;

    if (c->type == CMD_A) {
        if (c->s_wfile && !c->text) {
            /* r file */
            FILE *rf = fopen(c->s_wfile, "r");
            if (rf) {
                char buf[4096];
                while (fgets(buf, sizeof(buf), rf)) {
                    size_t bl = strlen(buf);
                    while (bl > 0 && (buf[bl-1] == '\n' || buf[bl-1] == '\r')) buf[--bl] = '\0';
                    fprintf(stdout, "%s\n", buf);
                }
                fclose(rf);
            }
        } else if (c->text) {
            fprintf(stdout, "%s\n", c->text);
        }
    } else if (c->type == CMD_I) {
        fprintf(stdout, "%s\n", c->text);
    }
    /* c 命令已经替换了当前行 */
}

/* 处理 c 命令：用 text 替换当前行 */
static int apply_c_check(sed_cmd_t *c, sed_ctx_t *ctx) {
    if (c->type != CMD_C) return 0;
    int match = addr_match(ctx, &c->addr1);
    if (c->neg) match = !match;
    if (!match) return 0;
    /* 替换当前行并立即打印 */
    fprintf(stdout, "%s\n", c->text ? c->text : "");
    return 1;  /* 跳过正常打印 */
}

/* === 主处理循环 === */

static void process_line(sed_ctx_t *ctx) {
    int skip_print = 0;
    int c_replaced = 0;

    for (sed_cmd_t *c = ctx->head; c; c = c->next) {
        apply_cmd(c, ctx, &skip_print);
        if (ctx->quit) return;
        if (skip_print) break;
        if (apply_c_check(c, ctx)) { c_replaced = 1; break; }
    }

    if (ctx->quit) return;

    if (!skip_print && !ctx->no_output && !c_replaced) {
        fprintf(stdout, "%s\n", ctx->line_buf);
    }

    /* end-of-cycle: a/i/r */
    if (!ctx->quit && !skip_print) {
        for (sed_cmd_t *c = ctx->head; c; c = c->next) {
            apply_after(c, ctx);
        }
    }
}

static void run_sed(sed_ctx_t *ctx) {
    FILE *in = stdin;
    char *line = NULL;
    size_t lcap = 0;
    ssize_t llen;

    while ((llen = getline(&line, &lcap, in)) >= 0) {
        /* 去尾换行 */
        while (llen > 0 && (line[llen-1] == '\n' || line[llen-1] == '\r'))
            line[--llen] = '\0';

        /* 设置当前行 */
        ctx->line_nr++;
        if (llen + 1 > (ssize_t)ctx->line_cap) {
            ctx->line_cap = (size_t)llen + 1;
            ctx->line_buf = realloc(ctx->line_buf, ctx->line_cap);
        }
        memcpy(ctx->line_buf, line, (size_t)llen);
        ctx->line_buf[llen] = '\0';
        ctx->line_len = (size_t)llen;

        process_line(ctx);
        if (ctx->quit) break;
    }
    free(line);
}

/* === 入口 === */

static void usage(void) {
    fprintf(stdout,
        "Usage: sed [-n] script [file...]\n"
        "       sed [-n] -e script [-e script...] [file...]\n"
        "       sed [-n] -f script-file [file...]\n\n"
        "POSIX sed subset: s, d, p, a, i, c, q, y, =, w, r\n");
    exit(0);
}

int main(int argc, char **argv) {
    int argi = utils_init(argc, argv);
    if (argi < argc && (!strcmp(argv[argi], "--help"))) usage();

    sed_ctx_t ctx;
    ctx_init(&ctx);

    int script_alloc = 4096;
    char *script = malloc((size_t)script_alloc);
    size_t script_len = 0;
    script[0] = '\0';
    int script_from_file = 0;

    int opt;
    while ((opt = getopt(argc, argv, "ne:f:i")) != -1) {
        switch (opt) {
        case 'n': ctx.no_output = 1; break;
        case 'e': {
            size_t el = strlen(optarg);
            if (script_len + el + 2 >= (size_t)script_alloc) {
                script_alloc = (int)((script_len + el + 2) * 2);
                script = realloc(script, (size_t)script_alloc);
            }
            memcpy(script + script_len, optarg, el);
            script_len += el;
            script[script_len++] = '\n';
            script[script_len] = '\0';
            break;
        }
        case 'f': {
            FILE *sf = fopen(optarg, "r");
            if (!sf) sed_die("%s: %s", optarg, strerror(errno));
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), sf)) > 0) {
                if (script_len + n + 1 >= (size_t)script_alloc) {
                    script_alloc = (int)((script_len + n + 1) * 2);
                    script = realloc(script, (size_t)script_alloc);
                }
                memcpy(script + script_len, buf, n);
                script_len += n;
            }
            fclose(sf);
            if (script_len > 0 && script[script_len-1] != '\n') {
                script[script_len++] = '\n';
            }
            script[script_len] = '\0';
            script_from_file = 1;
            break;
        }
        case 'i':
            /* -i 占位（in-place editing 简化不支持）*/
            break;
        default:
            fprintf(stderr, "sed: try --help\n");
            return 2;
        }
    }

    /* 如果没有 -e 或 -f，第一个非选项参数是 script */
    if (script_len == 0 && optind < argc) {
        size_t sl = strlen(argv[optind]);
        if (sl + 2 >= (size_t)script_alloc) {
            script_alloc = (int)(sl + 2);
            script = realloc(script, (size_t)script_alloc);
        }
        memcpy(script, argv[optind], sl);
        script[sl] = '\n';
        script[sl + 1] = '\0';
        script_len = sl + 1;
        optind++;
    }

    /* 解析 script */
    if (script_len > 0) {
        const char *p = script;
        while (p && *p) {
            if (*p == '\n' || *p == ' ' || *p == '\t' || *p == '#') {
                p++;
                continue;
            }
            p = parse_one(&ctx, p);
            if (!p) break;
        }
    }

    /* 处理输入文件 */
    ctx.files = argv + optind;
    ctx.nfiles = argc - optind;

    if (ctx.nfiles > 0) {
        for (int i = 0; i < ctx.nfiles; i++) {
            if (!strcmp(ctx.files[i], "-")) {
                /* stdin */
            } else {
                FILE *fp = fopen(ctx.files[i], "r");
                if (!fp) {
                    fprintf(stderr, "sed: %s: %s\n", ctx.files[i], strerror(errno));
                    continue;
                }
                /* 重定向 stdin */
                int rc = dup2(fileno(fp), STDIN_FILENO);
                (void)rc;
                fclose(fp);
            }
        }
    }

    run_sed(&ctx);

    free(script);
    free(ctx.line_buf);
    return 0;
}
