/* msh 语法分析 + AST + 执行
 *
 * 单文件实现：parse + AST 构造 + exec 求值（含变量展开、重定向、管道）。
 * 不实现：函数、case、算术展开、here-doc 内容。
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <getopt.h>
#include <glob.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "msh/lex.h"
#include "msh/exec.h"
#include "msh/job.h"
#include "msh/msh.h"
#include "msh/parse.h"
#include "msh/array.h"
#include "msh/plugin.h"

/* === AST 构造 === */

static ast_t *ast_new(int type) {
    ast_t *a = calloc(1, sizeof(*a));
    a->type = type;
    return a;
}

/* === 函数表 === */
typedef struct msh_func {
    char *name;
    ast_t *body;
    struct msh_func *next;
} msh_func_t;
static msh_func_t *g_funcs = NULL;

static ast_t *func_lookup(const char *name) {
    for (msh_func_t *f = g_funcs; f; f = f->next) {
        if (!strcmp(f->name, name)) return f->body;
    }
    return NULL;
}

static void func_add(const char *name, ast_t *body) {
    /* 覆盖同名函数 */
    msh_func_t *f;
    for (f = g_funcs; f; f = f->next) {
        if (!strcmp(f->name, name)) { f->body = body; return; }
    }
    f = calloc(1, sizeof(*f));
    f->name = strdup(name);
    f->body = body;
    f->next = g_funcs;
    g_funcs = f;
}

static void ast_push_arg(ast_t *cmd, const char *s) {
    cmd->argv = realloc(cmd->argv, sizeof(char *) * (cmd->argc + 2));
    cmd->argv[cmd->argc++] = strdup(s);
    cmd->argv[cmd->argc] = NULL;
}

void ast_free(ast_t *a) {
    if (!a) return;
    ast_free(a->left);
    ast_free(a->right);
    for (int i = 0; i < a->argc; i++) free(a->argv[i]);
    free(a->argv);
    redirect_t *r = a->redir;
    while (r) {
        redirect_t *next = r->next;
        free(r->target);
        free(r->heredoc);
        free(r);
        r = next;
    }
    /* 控制流字段 */
    free(a->str_val);
    ast_free(a->cond);
    ast_free(a->then_body);
    ast_free(a->else_body);
    ast_free(a->body);
    for (int i = 0; i < a->nitems; i++) ast_free(a->items[i]);
    free(a->items);
    for (int i = 0; i < a->npatterns; i++) free(a->patterns[i]);
    free(a->patterns);
    free(a);
}

/* === Token 队列（支持 1-lookahead 解析） === */

typedef struct {
    int tok;
    char *text;
} token_t;

typedef struct {
    lexer_t *lx;
    token_t cur;
    token_t peek;
    int has_peek;
    int err;
} parser_t;

static void take(parser_t *p) {
    if (p->has_peek) {
        p->cur = p->peek;
        p->peek.text = NULL;  /* 防止 cur/peek 共享指针导致 double-free */
        p->peek.tok = 0;
        p->has_peek = 0;
        return;
    }
    p->cur.text = NULL;
    p->cur.tok = msh_lex_next(p->lx, &p->cur.text);
}

static void consume_token(parser_t *p) {
    if (p->cur.text) { free(p->cur.text); p->cur.text = NULL; }
    take(p);
}

/* 1-token lookahead（不消费 cur）。 */
static int peek_tok(parser_t *p) {
    if (!p->has_peek) {
        p->peek.text = NULL;
        p->peek.tok = msh_lex_next(p->lx, &p->peek.text);
        p->has_peek = 1;
    }
    return p->peek.tok;
}

/* === 展开变量与命令替换 ===
 *
 * 实现见 src/var/expand.c（含 ${VAR:-default} 等修饰、$(...)、$((...))、tilde）。
 * 本文件仅保留 argv 层的 glob 包装。*/

#include "msh/expand.h"

/* 对一个 argv 元素做展开 + tilde + glob。
 * 返回新 argv 段（malloc 数组，末尾 NULL），*cnt_out 是元素数。
 * glob 无匹配时保留原字面（POSIX sh 行为）。*/
static char **expand_one_arg(const char *arg, int *cnt_out) {
    /* tilde：仅独立 ~ 或 ~/... 在 word 开头时替换 $HOME */
    char *tilde_buf = NULL;
    const char *expand_src = arg;
    if (arg[0] == '~') {
        const char *home = getenv("HOME");
        if (home && (arg[1] == '\0' || arg[1] == '/')) {
            size_t hl = strlen(home);
            tilde_buf = malloc(hl + strlen(arg + 1) + 1);
            memcpy(tilde_buf, home, hl);
            strcpy(tilde_buf + hl, arg + 1);
            expand_src = tilde_buf;
        }
    }
    char *expanded = msh_expand(expand_src);
    free(tilde_buf);

    /* glob：含通配符时调 libc glob()，无匹配保留字面 */
    char **result = NULL;
    int cnt = 0;
    if (strpbrk(expanded, "*?[")) {
        glob_t g;
        memset(&g, 0, sizeof(g));
        int grc = glob(expanded, 0, NULL, &g);
        if (grc == 0 && g.gl_pathc > 0) {
            result = malloc(sizeof(char *) * (g.gl_pathc + 1));
            for (size_t i = 0; i < g.gl_pathc; i++) {
                result[cnt++] = strdup(g.gl_pathv[i]);
            }
            result[cnt] = NULL;
        } else {
            result = malloc(sizeof(char *) * 2);
            result[cnt++] = expanded ? strdup(expanded) : strdup("");
            result[cnt] = NULL;
        }
        globfree(&g);
    } else {
        result = malloc(sizeof(char *) * 2);
        result[cnt++] = expanded ? strdup(expanded) : strdup("");
        result[cnt] = NULL;
    }
    free(expanded);
    *cnt_out = cnt;
    return result;
}

/* 词法已 collect 字符串。本函数仅做展开（不含 glob/tilde）。 */
static char *expand_string(const char *s) {
    if (!s) return NULL;
    return msh_expand(s);
}

/* === 解析：完整脚本 = 命令列表 === */

static ast_t *parse_command(parser_t *p);
static ast_t *parse_pipeline(parser_t *p);
static ast_t *parse_list(parser_t *p);
static ast_t *parse_compound(parser_t *p);
static ast_t *parse_list_until(parser_t *p, const char *const *kws);
static ast_t *parse_elif(parser_t *p);

/* 跳过空白 NEWLINE */
static void skip_newlines(parser_t *p) {
    while (p->cur.tok == TOK_NEWLINE) consume_token(p);
}

/* 检查当前 token 是否为指定保留字。
 * 因为 lex 仅在命令起始位置识别保留字，但实际很多上下文（for i in ...）中
 * `in`/`do`/`done`/`then`/`fi` 等不在命令起始位置，所以 lex 会返回 TOK_WORD。
 * 本 helper 同时接受 TOK_* 形式和 TOK_WORD 文本匹配。*/
static int is_kw(parser_t *p, int tok, const char *kw) {
    if (p->cur.tok == tok) return 1;
    if (p->cur.tok == TOK_WORD && p->cur.text && !strcmp(p->cur.text, kw)) return 1;
    return 0;
}

static int consume_kw(parser_t *p, int tok, const char *kw) {
    if (!is_kw(p, tok, kw)) return 0;
    consume_token(p);
    return 1;
}

static int is_assignment(const char *s) {
    /* VAR=val 形式：首个 = 不在开头 */
    if (!s || !*s || *s == '=') return 0;
    const char *eq = strchr(s, '=');
    if (!eq || eq == s) return 0;
    /* Check for bash array syntax: arr[0]=val or arr=(...) */
    for (const char *p = s; p < eq; p++) {
        if (p[0] == '[' && p < eq) {
            /* arr[idx]=val */
            const char *rb = strchr(p, ']');
            if (rb && rb < eq) {
                /* validate name before [ */
                for (const char *q = s; q < p; q++) {
                    if (!isalnum((unsigned char)*q) && *q != '_') return 0;
                }
                return 1;
            }
            return 0;
        }
        if (!isalnum((unsigned char)*p) && *p != '_') return 0;
    }
    /* Check for arr=(...) array creation */
    if (eq[1] == '(') return 1;
    return 1;
}


/* === 控制流/复合命令解析 ===
 *
 * parse_compound 是 parse_pipeline 的子层，识别命令起始位置的保留字。
 * parse_list_until 解析到任一 stop 关键字为止。
 */

/* 解析一个直到 stop 关键字（TOK_* 或 TOK_WORD 文本匹配）之前的命令列表。
 * kws 是 NULL 终止的字符串数组。不消费 stop token。*/
static ast_t *parse_list_until(parser_t *p, const char *const *kws) {
    /* 跳过起始 NEWLINE（如 `do\n echo` 中 do 后的换行） */
    while (p->cur.tok == TOK_NEWLINE) consume_token(p);
    ast_t *left = parse_pipeline(p);
    if (!left) return NULL;
    while (1) {
        int t = p->cur.tok;
        if (t == TOK_EOF || t == TOK_RPAREN) break;
        /* 命中 stop 关键字：保留字 token 或 TOK_WORD 的 text 都匹配 */
        if (p->cur.text) {
            int hit = 0;
            for (const char *const *k = kws; *k; k++) {
                if (!strcmp(p->cur.text, *k)) { hit = 1; break; }
            }
            if (hit) break;
        }
        /* TOK_THEN/FI 等被 lex 识别的 token：仅当在 kws 里才停。
         * 注意：嵌套 if 的 then 不该停 then_body 解析，所以这里不做硬编码 stop。
         * 但是 RPAREN/RBRACE 在 parse_compound 里已消费，外层不应遇到。 */
        if (t == TOK_AND || t == TOK_OR || t == TOK_SEMI || t == TOK_NEWLINE) {
            int op = t;
            consume_token(p);
            skip_newlines(p);
            /* 若紧接着就是 stop（kws 命中），则尾随分隔符，直接返回。
             * 保留字可能以 TOK_WORD 或保留字 token 形式出现，text 都非 NULL。 */
            int stop_now = 0;
            if (p->cur.text) {
                for (const char *const *k = kws; *k; k++) {
                    if (!strcmp(p->cur.text, *k)) { stop_now = 1; break; }
                }
            }
            if (stop_now) break;
            if (p->cur.tok == TOK_EOF) break;
            ast_t *right = parse_pipeline(p);
            if (!right) break;
            ast_t *node = ast_new(AST_LIST);
            node->left = left;
            node->right = right;
            node->list_op = op;
            left = node;
            continue;
        }
        break;
    }
    return left;
}

static ast_t *parse_if(parser_t *p) {
    consume_token(p);  /* if */
    const char *kws_cond[] = { "then", NULL };
    ast_t *node = ast_new(AST_IF);
    node->cond = parse_list_until(p, kws_cond);
    if (!consume_kw(p, TOK_THEN, "then")) { ast_free(node); return NULL; }
    const char *kws_then[] = { "elif", "else", "fi", NULL };
    node->then_body = parse_list_until(p, kws_then);
    if (is_kw(p, TOK_ELIF, "elif")) {
        /* elif 等价于嵌套的 if：elif cond then body [else body] 挂到 else_body。
         * 这里不消费最终 fi（由最外层消费）。 */
        node->else_body = parse_elif(p);
    } else if (consume_kw(p, TOK_ELSE, "else")) {
        const char *kws_else[] = { "fi", NULL };
        node->else_body = parse_list_until(p, kws_else);
    }
    if (!consume_kw(p, TOK_FI, "fi")) { ast_free(node); return NULL; }
    return node;
}

/* 解析 elif 后续链：elif cond then body [elif...|else body]。不消费最终 fi。 */
static ast_t *parse_elif(parser_t *p) {
    consume_kw(p, TOK_ELIF, "elif");  /* 已确认是 elif */
    const char *kws_cond[] = { "then", NULL };
    ast_t *node = ast_new(AST_IF);
    node->cond = parse_list_until(p, kws_cond);
    if (!consume_kw(p, TOK_THEN, "then")) { ast_free(node); return NULL; }
    const char *kws_then[] = { "elif", "else", "fi", NULL };
    node->then_body = parse_list_until(p, kws_then);
    if (is_kw(p, TOK_ELIF, "elif")) {
        node->else_body = parse_elif(p);  /* 递归 elif */
    } else if (consume_kw(p, TOK_ELSE, "else")) {
        const char *kws_else[] = { "fi", NULL };
        node->else_body = parse_list_until(p, kws_else);
    }
    return node;  /* 不消费 fi */
}

static ast_t *parse_for(parser_t *p) {
    consume_token(p);  /* for */
    if (p->cur.tok != TOK_WORD) return NULL;
    ast_t *node = ast_new(AST_FOR);
    node->str_val = strdup(p->cur.text);
    consume_token(p);  /* varname */
    skip_newlines(p);
    /* 可选 in wordlist */
    if (consume_kw(p, TOK_IN, "in")) {
        while (p->cur.tok == TOK_WORD) {
            node->patterns = realloc(node->patterns, sizeof(char *) * (node->npatterns + 1));
            node->patterns[node->npatterns++] = strdup(p->cur.text);
            consume_token(p);
        }
        if (p->cur.tok == TOK_SEMI || p->cur.tok == TOK_NEWLINE) consume_token(p);
    } else if (p->cur.tok == TOK_SEMI || p->cur.tok == TOK_NEWLINE) {
        consume_token(p);
    }
    if (!consume_kw(p, TOK_DO, "do")) { ast_free(node); return NULL; }
    const char *kws[] = { "done", NULL };
    node->body = parse_list_until(p, kws);
    if (!consume_kw(p, TOK_DONE, "done")) { ast_free(node); return NULL; }
    return node;
}

static ast_t *parse_while(parser_t *p, int is_until) {
    consume_token(p);  /* while / until */
    const char *kws_cond[] = { "do", NULL };
    ast_t *node = ast_new(is_until ? AST_UNTIL : AST_WHILE);
    node->cond = parse_list_until(p, kws_cond);
    if (!consume_kw(p, TOK_DO, "do")) { ast_free(node); return NULL; }
    const char *kws_body[] = { "done", NULL };
    node->body = parse_list_until(p, kws_body);
    if (!consume_kw(p, TOK_DONE, "done")) { ast_free(node); return NULL; }
    return node;
}

static ast_t *parse_case(parser_t *p) {
    consume_token(p);  /* case */
    if (p->cur.tok != TOK_WORD) return NULL;
    ast_t *node = ast_new(AST_CASE);
    node->str_val = strdup(p->cur.text);
    consume_token(p);  /* word */
    skip_newlines(p);
    if (!consume_kw(p, TOK_IN, "in")) { ast_free(node); return NULL; }
    skip_newlines(p);
    while (!is_kw(p, TOK_ESAC, "esac") && p->cur.tok != TOK_EOF) {
        if (p->cur.tok != TOK_WORD) { ast_free(node); return NULL; }
        char *pat = strdup(p->cur.text);
        consume_token(p);
        if (p->cur.tok != TOK_RPAREN) { free(pat); ast_free(node); return NULL; }
        consume_token(p);  /* ) */
        const char *kws[] = { "esac", NULL };
        ast_t *body = parse_list_until(p, kws);
        node->patterns = realloc(node->patterns, sizeof(char *) * (node->npatterns + 1));
        node->patterns[node->npatterns++] = pat;
        node->items = realloc(node->items, sizeof(ast_t *) * (node->nitems + 1));
        node->items[node->nitems++] = body;
        /* ;; 分隔符 */
        while (p->cur.tok == TOK_SEMI) consume_token(p);
        skip_newlines(p);
    }
    if (!consume_kw(p, TOK_ESAC, "esac")) { ast_free(node); return NULL; }
    return node;
}

static ast_t *parse_brace_group(parser_t *p) {
    consume_token(p);  /* { */
    const char *kws[] = { "}", NULL };
    ast_t *node = ast_new(AST_BRACE_GROUP);
    node->body = parse_list_until(p, kws);
    if (!consume_kw(p, TOK_RBRACE, "}")) { ast_free(node); return NULL; }
    return node;
}

static ast_t *parse_subshell(parser_t *p) {
    consume_token(p);  /* ( */
    /* 子 shell 不用关键字 stop，直接 parse_list（遇到 RPAREN 自然停） */
    ast_t *node = ast_new(AST_SUBSHELL);
    /* 临时：用一个空 kws 数组，让 parse_list_until 在 RPAREN 自然停 */
    const char *kws[] = { NULL };
    node->body = parse_list_until(p, kws);
    if (p->cur.tok != TOK_RPAREN) { ast_free(node); return NULL; }
    consume_token(p);  /* ) */
    return node;
}

static ast_t *parse_func(parser_t *p) {
    /* 当前 token 是 TOK_FUNCTION 或 TOK_WORD（后跟 '('），或 TOK_WORD 后跟 '{' */
    ast_t *node = ast_new(AST_FUNC);
    if (p->cur.tok == TOK_FUNCTION) {
        consume_token(p);
        if (p->cur.tok != TOK_WORD) { ast_free(node); return NULL; }
        node->str_val = strdup(p->cur.text);
        consume_token(p);
        /* 可选 () */
        if (p->cur.tok == TOK_LPAREN) {
            consume_token(p);
            if (p->cur.tok != TOK_RPAREN) { ast_free(node); return NULL; }
            consume_token(p);
        }
    } else {
        /* WORD ( ) */
        node->str_val = strdup(p->cur.text);
        consume_token(p);
        if (p->cur.tok != TOK_LPAREN) { ast_free(node); return NULL; }
        consume_token(p);
        if (p->cur.tok != TOK_RPAREN) { ast_free(node); return NULL; }
        consume_token(p);
    }
    skip_newlines(p);
    /* body 必须是 { ... } 块。`{` 在命令起始位置才被 lex 识别为 TOK_LBRACE；
     * 但在 `function name` 之后 at_cmd_start=0，所以 `{` 通常是 TOK_WORD。
     * 接受两种形式。 */
    if (!is_kw(p, TOK_LBRACE, "{")) { ast_free(node); return NULL; }
    consume_token(p);  /* { */
    {
        const char *kws[] = { "}", NULL };
        node->body = parse_list_until(p, kws);
    }
    if (!is_kw(p, TOK_RBRACE, "}")) { ast_free(node); return NULL; }
    consume_token(p);  /* } */
    return node;
}

/* parse_compound：识别命令起始位置的保留字，否则委托 parse_command */
/* parse_dbracket: bash [[ expr ]] conditional */
static ast_t *parse_dbracket(parser_t *p) {
    consume_token(p); /* consume TOK_DLBRACK */
    
    ast_t *node = ast_new(AST_CMD);
    ast_push_arg(node, "[[");
    
    /* Collect words until ]] (may be TOK_DRBRACK or TOK_WORD "]]") */
    while (p->cur.tok != TOK_EOF && p->cur.tok != TOK_NEWLINE) {
        if (p->cur.tok == TOK_DRBRACK) break;
        if (p->cur.tok == TOK_WORD && p->cur.text && strcmp(p->cur.text, "]]") == 0)
            break;
        if (p->cur.text) {
            ast_push_arg(node, p->cur.text);
        }
        consume_token(p);
    }
    
    /* Consume ]] */
    consume_token(p);
    
    return node;
}

static ast_t *parse_compound(parser_t *p) {
    switch (p->cur.tok) {
    case TOK_IF:       return parse_if(p);
    case TOK_FOR:      return parse_for(p);
    case TOK_WHILE:    return parse_while(p, 0);
    case TOK_UNTIL:    return parse_while(p, 1);
    case TOK_CASE:     return parse_case(p);
    case TOK_LBRACE:   return parse_brace_group(p);
    case TOK_LPAREN:   return parse_subshell(p);
    case TOK_FUNCTION: return parse_func(p);
    case TOK_DLBRACK:  return parse_dbracket(p);
    case TOK_WORD:
        /* [[ bash conditional (fallback when not at cmd_start) */
        if (p->cur.text && strcmp(p->cur.text, "[[") == 0) return parse_dbracket(p);
        /* 2-lookahead：name() { ... } 形式的函数定义 */
        if (peek_tok(p) == TOK_LPAREN) return parse_func(p);
        return parse_command(p);
    default:           return NULL;
    }
}

static ast_t *parse_command(parser_t *p) {
    if (p->cur.tok != TOK_WORD) return NULL;
    /* 解析前导 assignments（VAR=val args） */
    ast_t *first_assign = NULL;
    ast_t **tail = &first_assign;
    while (p->cur.tok == TOK_WORD && is_assignment(p->cur.text)) {
        ast_t *asn = ast_new(AST_ASSIGN);
        ast_push_arg(asn, p->cur.text);  /* argv[0] = "VAR=val" */
        consume_token(p);
        *tail = asn;
        tail = &asn->right;
    }
    if (p->cur.tok != TOK_WORD) {
        /* 纯赋值（无命令） */
        return first_assign;
    }

    ast_t *cmd = ast_new(AST_CMD);
    ast_push_arg(cmd, p->cur.text);  /* 原始，待 exec 时展开 */
    consume_token(p);
    while (p->cur.tok == TOK_WORD) {
        ast_push_arg(cmd, p->cur.text);
        consume_token(p);
    }
    /* 后随的重定向 */
    while (p->cur.tok == TOK_REDIR_IN || p->cur.tok == TOK_REDIR_OUT
           || p->cur.tok == TOK_REDIR_APPEND || p->cur.tok == TOK_HEREDOC
           || p->cur.tok == TOK_HERESTRING
           || p->cur.tok == TOK_REDIR_DUP_IN || p->cur.tok == TOK_REDIR_DUP_OUT) {
        int op = p->cur.tok;
        consume_token(p);
        if (p->cur.tok != TOK_WORD) { ast_free(cmd); return NULL; }
        redirect_t *r = calloc(1, sizeof(*r));
        r->op = op;
        r->target = strdup(p->cur.text);  /* 原始，待 exec 时展开 */
        consume_token(p);
        /* heredoc：扫描后续行直到 delimiter */
        if (op == TOK_HEREDOC) {
            /* 词法器已剥离引号；通过 was_quoted 判断 delimiter 是否被引号包裹。
             * 引号 delimiter → 内容不展开；无引号 → 展开变量/命令替换。*/
            int quoted = p->lx->was_quoted;  /* 1=单引号, 2=双引号, 0=无引号 */
            r->heredoc = msh_lex_heredoc(p->lx, r->target);
            r->fd = quoted;  /* 复用 fd 字段为 quoted 标志 */
        }
        r->next = cmd->redir;
        cmd->redir = r;
    }
    /* 链：assignments + command。 chain assignments 到 cmd */
    if (first_assign) {
        ast_t *prev = first_assign;
        while (prev->right) prev = prev->right;
        prev->right = cmd;
        return first_assign;
    }
    return cmd;
}

static ast_t *parse_pipeline(parser_t *p) {
    ast_t *first = parse_compound(p);
    if (!first) return NULL;
    while (p->cur.tok == TOK_PIPE) {
        consume_token(p);
        ast_t *right = parse_command(p);
        if (!right) { ast_free(first); return NULL; }
        ast_t *p2 = ast_new(AST_PIPE);
        p2->left = first;
        p2->right = right;
        first = p2;
    }
    return first;
}

static ast_t *parse_list(parser_t *p) {
    /* 跳过起始的空白/NEWLINE（注释行也产出 NEWLINE） */
    while (p->cur.tok == TOK_NEWLINE) consume_token(p);
    ast_t *left = parse_pipeline(p);
    if (!left) return NULL;
    /* 后台 & ：把 left 包装为 AST_BG */
    if (p->cur.tok == TOK_BG) {
        consume_token(p);
        ast_t *bg = ast_new(AST_BG);
        bg->left = left;
        left = bg;
    }
    while (p->cur.tok == TOK_AND || p->cur.tok == TOK_OR
           || p->cur.tok == TOK_SEMI || p->cur.tok == TOK_NEWLINE) {
        int op = p->cur.tok;
        consume_token(p);
        if (op == TOK_NEWLINE || op == TOK_SEMI) {
            /* 跳过空行 */
            while (p->cur.tok == TOK_NEWLINE) consume_token(p);
            if (p->cur.tok == TOK_EOF) break;
            /* 继续解析下一条命令 */
        }
        ast_t *right = parse_pipeline(p);
        if (!right) break;
        /* 后台 & 出现在 right 之后：cmd1 & cmd2 */
        if (p->cur.tok == TOK_BG) {
            consume_token(p);
            ast_t *bg = ast_new(AST_BG);
            bg->left = right;
            right = bg;
        }
        ast_t *node = ast_new(AST_LIST);
        node->left = left;
        node->right = right;
        node->list_op = op;
        left = node;
    }
    return left;
}

ast_t *msh_parse(lexer_t *lx) {
    parser_t p = { .lx = lx, .has_peek = 0 };
    take(&p);
    ast_t *root = parse_list(&p);
    if (p.cur.text) free(p.cur.text);
    if (p.peek.text) free(p.peek.text);
    return root;
}

/* === 执行 === */

/* 内建命令表（委托 builtin.c，本骨架 stub） */
extern int msh_builtin_cd(int argc, char **argv);
extern int msh_builtin_export(int argc, char **argv);
extern int msh_builtin_unset(int argc, char **argv);
extern int msh_builtin_set(int argc, char **argv);

static int is_builtin(const char *name) {
    if (!name) return 0;
    return !strcmp(name, "cd") || !strcmp(name, "export")
        || !strcmp(name, "unset") || !strcmp(name, "set")
        || !strcmp(name, "exit") || !strcmp(name, "true")
        || !strcmp(name, "false") || !strcmp(name, ":")
        || !strcmp(name, "echo") || !strcmp(name, "pwd")
        || !strcmp(name, "read") || !strcmp(name, "eval")
        || !strcmp(name, "type") || !strcmp(name, "exec")
        || !strcmp(name, "jobs") || !strcmp(name, "fg")
        || !strcmp(name, "bg") || !strcmp(name, "wait")
        || !strcmp(name, "trap") || !strcmp(name, "source")
        || !strcmp(name, ".") || !strcmp(name, "msh")
        || !strcmp(name, "[[")
        || !strcmp(name, "complete") || !strcmp(name, "compgen")
        || !strcmp(name, "shift") || !strcmp(name, "return")
        || !strcmp(name, "break") || !strcmp(name, "continue")
        || !strcmp(name, "alias") || !strcmp(name, "unalias")
        || !strcmp(name, "local") || !strcmp(name, "getopts")
        || !strcmp(name, "umask") || !strcmp(name, "hash")
        || !strcmp(name, "let") || !strcmp(name, "declare")
        || !strcmp(name, "typeset")
        /* === Shell-Utils 联动：高性能内建 === */
        || !strcmp(name, "printf")
        || !strcmp(name, "test") || !strcmp(name, "[")
        || !strcmp(name, "sleep")
        || !strcmp(name, "seq");
}

int msh_run_builtin(ast_t *ast, int argc, char **argv) {
    const char *name = argv[0];
    
    /* [[ ... ]] bash conditional expression */
    if (strcmp(name, "[[") == 0) {
        /* Evaluate expression: argc-2 args (skip "[[" and last "]]" not included) */
        /* Build test-like argv for our test implementation */
        char **targv = malloc(sizeof(char*) * (argc + 1));
        targv[0] = "test";
        for (int i = 1; i < argc; i++) targv[i] = argv[i];
        targv[argc] = NULL;
        
        /* Use the test builtin logic (reuse from utils test.c) */
        /* Simple evaluation for common cases */
        int result = 1; /* default false */
        
        if (argc == 2) {
            /* [[ str ]] — true if non-empty */
            result = (strlen(argv[1]) > 0) ? 0 : 1;
        } else if (argc == 3) {
            /* [[ -flag str ]] */
            const char *op = argv[1];
            const char *arg = argv[2];
            if (strcmp(op, "-z") == 0) result = (strlen(arg) == 0) ? 0 : 1;
            else if (strcmp(op, "-n") == 0) result = (strlen(arg) > 0) ? 0 : 1;
            else if (strcmp(op, "-f") == 0) {
                struct stat st;
                result = (stat(arg, &st) == 0 && S_ISREG(st.st_mode)) ? 0 : 1;
            }
            else if (strcmp(op, "-d") == 0) {
                struct stat st;
                result = (stat(arg, &st) == 0 && S_ISDIR(st.st_mode)) ? 0 : 1;
            }
            else if (strcmp(op, "-e") == 0) {
                result = (access(arg, F_OK) == 0) ? 0 : 1;
            }
            else if (strcmp(op, "-r") == 0) {
                result = (access(arg, R_OK) == 0) ? 0 : 1;
            }
            else if (strcmp(op, "-w") == 0) {
                result = (access(arg, W_OK) == 0) ? 0 : 1;
            }
            else if (strcmp(op, "-x") == 0) {
                result = (access(arg, X_OK) == 0) ? 0 : 1;
            }
            else if (strcmp(op, "!") == 0) {
                /* [[ ! str ]] — negate non-empty test */
                result = (strlen(arg) == 0) ? 0 : 1;
            }
        } else if (argc == 4) {
            /* [[ a op b ]] */
            const char *a = argv[1];
            const char *op = argv[2];
            const char *b = argv[3];
            
            if (strcmp(op, "==") == 0 || strcmp(op, "=") == 0)
                result = (strcmp(a, b) == 0) ? 0 : 1;
            else if (strcmp(op, "!=") == 0)
                result = (strcmp(a, b) != 0) ? 0 : 1;
            else if (strcmp(op, "-eq") == 0)
                result = (atoi(a) == atoi(b)) ? 0 : 1;
            else if (strcmp(op, "-ne") == 0)
                result = (atoi(a) != atoi(b)) ? 0 : 1;
            else if (strcmp(op, "-lt") == 0)
                result = (atoi(a) < atoi(b)) ? 0 : 1;
            else if (strcmp(op, "-le") == 0)
                result = (atoi(a) <= atoi(b)) ? 0 : 1;
            else if (strcmp(op, "-gt") == 0)
                result = (atoi(a) > atoi(b)) ? 0 : 1;
            else if (strcmp(op, "-ge") == 0)
                result = (atoi(a) >= atoi(b)) ? 0 : 1;
            else if (strcmp(op, "=~") == 0) {
                /* regex match — simplified */
                result = (strstr(a, b) != NULL) ? 0 : 1;
            }
        }
        
        free(targv);
        msh_last_status = result;
        return result;
    }
    
    if (argc == 0) { return 0; }
    if (!strcmp(name, ":")) return 0;
    if (!strcmp(name, "true")) return 0;
    if (!strcmp(name, "false")) return 1;
    if (!strcmp(name, "exit")) {
        int rc = argc > 1 ? atoi(argv[1]) : 0;
        msh_set_exit(rc);
        msh_trap_exit();
        exit(rc);
    }
    if (!strcmp(name, "echo")) {
        int nl = 1;
        int first = 1;
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-n")) { nl = 0; continue; }
            if (!first) putchar(' ');
            first = 0;
            fputs(argv[i], stdout);
        }
        if (nl) putchar('\n');
        fflush(stdout);
        return 0;
    }
    if (!strcmp(name, "cd")) {
        return msh_builtin_cd(argc, argv);
    }
    if (!strcmp(name, "pwd")) {
        char buf[4096];
        if (getcwd(buf, sizeof(buf))) { puts(buf); return 0; }
        return 1;
    }
    if (!strcmp(name, "export")) {
        return msh_builtin_export(argc, argv);
    }
    if (!strcmp(name, "unset")) {
        return msh_builtin_unset(argc, argv);
    }
    if (!strcmp(name, "set")) {
        return msh_builtin_set(argc, argv);
    }
    if (!strcmp(name, "read")) {
        char buf[4096];
        if (!fgets(buf, sizeof(buf), stdin)) return 1;
        size_t L = strlen(buf);
        if (L && buf[L - 1] == '\n') buf[--L] = '\0';
        if (argc > 1) setenv(argv[1], buf, 1);
        return 0;
    }
    if (!strcmp(name, "eval")) {
        /* 简化：栈式合并 argv 并重新 parse */
        return 0;
    }
    if (!strcmp(name, "type")) {
        if (argc < 2) return 1;
        if (is_builtin(argv[1])) { printf("%s is a shell builtin\n", argv[1]); return 0; }
        /* 检查 PATH */
        return 1;
    }
    if (!strcmp(name, "exec")) {
        if (argc < 2) return 1;
        execvp(argv[1], &argv[1]);
        fprintf(stderr, "%s: exec %s: %s\n", "msh", argv[1], strerror(errno));
        return 127;
    }
    if (!strcmp(name, "jobs")) {
        msh_job_list();
        return 0;
    }
    if (!strcmp(name, "fg")) {
        int id = argc > 1 ? atoi(argv[1]) : -1;
        if (id < 0) { fprintf(stderr, "msh: fg: no job control\n"); return 1; }
        return msh_job_fg(id);
    }
    if (!strcmp(name, "bg")) {
        int id = argc > 1 ? atoi(argv[1]) : -1;
        if (id < 0) { fprintf(stderr, "msh: bg: no job control\n"); return 1; }
        return msh_job_bg(id);
    }
    if (!strcmp(name, "wait")) {
        int id = argc > 1 ? atoi(argv[1]) : 0;
        if (id == 0) { msh_job_reap(); return 0; }
        return msh_job_fg(id);
    }
    if (!strcmp(name, "trap")) {
        return msh_builtin_trap(argc, argv);
    }
    if (!strcmp(name, "source") || !strcmp(name, ".")) {
        /* source / . ：在当前 shell 执行脚本文件 */
        if (argc < 2) {
            fprintf(stderr, "msh: %s: filename argument required\n", name);
            return 2;
        }
        /* 仅第一个参数是文件，后续参数作为位置参数 */
        FILE *fp = fopen(argv[1], "r");
        if (!fp) {
            fprintf(stderr, "msh: %s: %s: %s\n", name, argv[1], strerror(errno));
            return 1;
        }
        char *buf = NULL;
        size_t cap = 4096, len = 0;
        buf = malloc(cap);
        for (;;) {
            if (len + 4096 >= cap) { cap *= 2; buf = realloc(buf, cap); }
            size_t n = fread(buf + len, 1, cap - len - 1, fp);
            len += n;
            if (n == 0) break;
        }
        buf[len] = '\0';
        fclose(fp);
        /* 保存/恢复位置参数 */
        int saved_argc = 0;
        char *saved_args[64] = {NULL};
        for (int j = 1; j < 64; j++) {
            char vn[16];
            snprintf(vn, sizeof(vn), "%d", j);
            const char *v = getenv(vn);
            if (v) { saved_args[j-1] = strdup(v); saved_argc = j; }
            else break;
        }
        /* 设置新的位置参数（source 的后续参数） */
        for (int j = 1; j < 64; j++) {
            char vn[16];
            snprintf(vn, sizeof(vn), "%d", j);
            if (j + 1 < argc) {
                setenv(vn, argv[j + 1], 1);
            } else {
                unsetenv(vn);
            }
        }
        int rc = msh_run_string(buf, len);
        free(buf);
        /* 恢复位置参数 */
        for (int j = 1; j < 64; j++) {
            char vn[16];
            snprintf(vn, sizeof(vn), "%d", j);
            if (j <= saved_argc) {
                setenv(vn, saved_args[j-1], 1);
                free(saved_args[j-1]);
            } else {
                unsetenv(vn);
            }
        }
        msh_last_status = rc;
        return msh_last_status;
    }
    if (!strcmp(name, "msh")) {
        /* msh meta-builtin: plugin/theme management */
        return msh_plugin_builtin(argc, argv);
    }
    if (!strcmp(name, "complete")) {
        int rc = msh_builtin_complete(argc, argv);
        msh_last_status = rc;
        return rc;
    }
    if (!strcmp(name, "compgen")) {
        int rc = msh_builtin_compgen(argc, argv);
        msh_last_status = rc;
        return rc;
    }
    /* === 循环控制 === */
    if (!strcmp(name, "break")) {
        int n = argc > 1 ? atoi(argv[1]) : 1;
        msh_break_flag = n;
        return 0;
    }
    if (!strcmp(name, "continue")) {
        int n = argc > 1 ? atoi(argv[1]) : 1;
        msh_continue_flag = n;
        return 0;
    }
    if (!strcmp(name, "return")) {
        int rc = argc > 1 ? atoi(argv[1]) : msh_last_status;
        msh_return_flag = 1;
        msh_return_value = rc;
        return rc;
    }
    /* === shift === */
    if (!strcmp(name, "shift")) {
        int n = argc > 1 ? atoi(argv[1]) : 1;
        /* 获取 $# */
        const char *cnt = getenv("#");
        int total = cnt ? atoi(cnt) : 0;
        if (n > total) { fprintf(stderr, "msh: shift: %d: shift count out of range\n", n); return 1; }
        /* 移动 $1..$(total-n) = $(n+1)..$total */
        for (int i = 1; i <= total - n; i++) {
            char vn_old[16], vn_new[16];
            snprintf(vn_new, sizeof(vn_new), "%d", i);
            snprintf(vn_old, sizeof(vn_old), "%d", i + n);
            const char *v = getenv(vn_old);
            if (v) setenv(vn_new, v, 1);
            else unsetenv(vn_new);
        }
        /* 清除尾部 */
        for (int i = total - n + 1; i <= total; i++) {
            char vn[16];
            snprintf(vn, sizeof(vn), "%d", i);
            unsetenv(vn);
        }
        /* 更新 $# */
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", total - n);
        setenv("#", buf, 1);
        return 0;
    }
    /* === alias / unalias === */
    if (!strcmp(name, "alias")) {
        if (argc == 1) { msh_alias_list(); return 0; }
        int rc = 0;
        for (int i = 1; i < argc; i++) {
            char *eq = strchr(argv[i], '=');
            if (eq) {
                *eq = '\0';
                msh_alias_add(argv[i], eq + 1);
                *eq = '=';
            } else {
                const char *v = msh_alias_lookup(argv[i]);
                if (v) printf("alias %s='%s'\n", argv[i], v);
                else { fprintf(stderr, "msh: alias: %s: not found\n", argv[i]); rc = 1; }
            }
        }
        return rc;
    }
    if (!strcmp(name, "unalias")) {
        for (int i = 1; i < argc; i++)
            msh_alias_remove(argv[i]);
        return 0;
    }
    /* === local (简化：等同普通赋值) === */
    if (!strcmp(name, "local") || !strcmp(name, "declare") || !strcmp(name, "typeset")) {
        for (int i = 1; i < argc; i++) {
            char *eq = strchr(argv[i], '=');
            if (eq) {
                *eq = '\0';
                setenv(argv[i], eq + 1, 1);
                *eq = '=';
            }
        }
        return 0;
    }
    /* === getopts === */
    if (!strcmp(name, "getopts")) {
        return msh_builtin_getopts(argc, argv);
    }
    /* === umask === */
    if (!strcmp(name, "umask")) {
        if (argc == 1) {
            mode_t old = umask(0);
            umask(old);
            printf("%03o\n", (unsigned)old);
            return 0;
        }
        /* 解析八进制或符号 */
        char *end;
        long m = strtol(argv[1], &end, 8);
        if (*end == '\0') {
            umask((mode_t)m);
        } else {
            /* 符号模式简化：直接用数字 */
            fprintf(stderr, "msh: umask: %s: invalid mask\n", argv[1]);
            return 1;
        }
        return 0;
    }
    /* === hash === */
    if (!strcmp(name, "hash")) {
        /* 简化：无命令哈希缓存，仅打印 "hash table is empty" */
        if (argc > 1 && !strcmp(argv[1], "-r")) return 0;  /* -r: 清除 */
        printf("hash commands table is empty\n");
        return 0;
    }
    /* === let === */
    if (!strcmp(name, "let")) {
        return msh_builtin_let(argc, argv);
    }
    /* === Shell-Utils 联动：高性能内建 === */

    /* printf 内建：避免 fork+exec 的格式化输出 */
    if (!strcmp(name, "printf")) {
        if (argc < 2) {
            fprintf(stderr, "msh: printf: missing format string\n");
            return 2;
        }
        const char *fmt = argv[1];
        /* 收集后续参数 */
        const char **args = NULL;
        int nargs = 0;
        if (argc > 2) {
            args = malloc(sizeof(char*) * (argc - 1));
            for (int i = 2; i < argc; i++) args[nargs++] = argv[i];
        }
        int arg_idx = 0;
        const char *p = fmt;
        while (*p) {
            if (*p == '\\') {
                /* 转义序列 */
                p++;
                switch (*p) {
                case 'n': putchar('\n'); break;
                case 't': putchar('\t'); break;
                case 'r': putchar('\r'); break;
                case '\\': putchar('\\'); break;
                case '0': putchar('\0'); break;
                case '\"': putchar('\"'); break;
                default: putchar('\\'); putchar(*p); break;
                }
                if (*p) p++;
            } else if (*p == '%') {
                p++;
                /* 跳过 flags/width/precision */
                while (*p && (strchr("-+ #0", *p) || (*p >= '0' && *p <= '9') || *p == '.' || *p == '*')) p++;
                char conv = *p;
                if (conv) p++;
                const char *arg = (arg_idx < nargs) ? args[arg_idx++] : "";
                switch (conv) {
                case 'd': case 'i': printf("%d", arg ? atoi(arg) : 0); break;
                case 's': fputs(arg ? arg : "", stdout); break;
                case 'c': putchar(arg ? arg[0] : '\0'); break;
                case 'x': printf("%x", arg ? (unsigned)atoi(arg) : 0); break;
                case 'X': printf("%X", arg ? (unsigned)atoi(arg) : 0); break;
                case 'o': printf("%o", arg ? (unsigned)atoi(arg) : 0); break;
                case 'u': printf("%u", arg ? (unsigned)atoi(arg) : 0); break;
                case 'f': case 'g': case 'e': printf("%g", arg ? atof(arg) : 0.0); break;
                case '%': putchar('%'); break;
                default: putchar('%'); putchar(conv); break;
                }
            } else {
                putchar(*p);
                p++;
            }
        }
        free(args);
        fflush(stdout);
        msh_last_status = 0;
        return 0;
    }
    /* test / [ 内建：POSIX 条件测试，避免 fork+exec */
    if (!strcmp(name, "test") || !strcmp(name, "[")) {
        int result = 1; /* 默认 false */
        int test_argc = argc;
        char **test_argv = argv;
        /* [ 命令需要以 ] 结尾 */
        if (!strcmp(name, "[")) {
            if (argc < 2 || strcmp(argv[argc - 1], "]") != 0) {
                fprintf(stderr, "msh: [: missing `]`\n");
                return 2;
            }
            test_argc = argc - 1; /* 去掉 ] */
        }
        /* 跳过命令名 */
        int n = test_argc - 1; /* 参数个数 */
        char **a = test_argv + 1; /* 参数从 argv[1] 开始 */
        if (n == 0) result = 1;
        else if (n == 1) result = (strlen(a[0]) > 0) ? 0 : 1;
        else if (n == 2) {
            /* 一元操作符 -flag arg */
            const char *op = a[0];
            const char *arg = a[1];
            struct stat st;
            if (!strcmp(op, "-z")) result = (strlen(arg) == 0) ? 0 : 1;
            else if (!strcmp(op, "-n")) result = (strlen(arg) > 0) ? 0 : 1;
            else if (!strcmp(op, "-e")) result = (access(arg, F_OK) == 0) ? 0 : 1;
            else if (!strcmp(op, "-f")) result = (stat(arg, &st) == 0 && S_ISREG(st.st_mode)) ? 0 : 1;
            else if (!strcmp(op, "-d")) result = (stat(arg, &st) == 0 && S_ISDIR(st.st_mode)) ? 0 : 1;
            else if (!strcmp(op, "-r")) result = (access(arg, R_OK) == 0) ? 0 : 1;
            else if (!strcmp(op, "-w")) result = (access(arg, W_OK) == 0) ? 0 : 1;
            else if (!strcmp(op, "-x")) result = (access(arg, X_OK) == 0) ? 0 : 1;
            else if (!strcmp(op, "-s")) result = (stat(arg, &st) == 0 && st.st_size > 0) ? 0 : 1;
            else if (!strcmp(op, "-h") || !strcmp(op, "-L")) result = (lstat(arg, &st) == 0 && S_ISLNK(st.st_mode)) ? 0 : 1;
            else if (!strcmp(op, "-p")) result = (stat(arg, &st) == 0 && S_ISFIFO(st.st_mode)) ? 0 : 1;
            else if (!strcmp(op, "-S")) result = (stat(arg, &st) == 0 && S_ISSOCK(st.st_mode)) ? 0 : 1;
            else if (!strcmp(op, "-b")) result = (stat(arg, &st) == 0 && S_ISBLK(st.st_mode)) ? 0 : 1;
            else if (!strcmp(op, "-c")) result = (stat(arg, &st) == 0 && S_ISCHR(st.st_mode)) ? 0 : 1;
            else if (!strcmp(op, "!")) result = (strlen(a[1]) > 0) ? 1 : 0;
            else { fprintf(stderr, "msh: test: %s: unknown operator\n", op); result = 2; }
        } else if (n == 3) {
            /* 二元操作符 a op b */
            const char *left = a[0];
            const char *op = a[1];
            const char *right = a[2];
            struct stat st1, st2;
            if (!strcmp(op, "=") || !strcmp(op, "==")) result = (strcmp(left, right) == 0) ? 0 : 1;
            else if (!strcmp(op, "!=")) result = (strcmp(left, right) != 0) ? 0 : 1;
            else if (!strcmp(op, "-eq")) result = (atoi(left) == atoi(right)) ? 0 : 1;
            else if (!strcmp(op, "-ne")) result = (atoi(left) != atoi(right)) ? 0 : 1;
            else if (!strcmp(op, "-lt")) result = (atoi(left) < atoi(right)) ? 0 : 1;
            else if (!strcmp(op, "-le")) result = (atoi(left) <= atoi(right)) ? 0 : 1;
            else if (!strcmp(op, "-gt")) result = (atoi(left) > atoi(right)) ? 0 : 1;
            else if (!strcmp(op, "-ge")) result = (atoi(left) >= atoi(right)) ? 0 : 1;
            else if (!strcmp(op, "-nt")) result = (stat(left, &st1) == 0 && stat(right, &st2) == 0 && st1.st_mtime > st2.st_mtime) ? 0 : 1;
            else if (!strcmp(op, "-ot")) result = (stat(left, &st1) == 0 && stat(right, &st2) == 0 && st1.st_mtime < st2.st_mtime) ? 0 : 1;
            else if (!strcmp(op, "-ef")) result = (stat(left, &st1) == 0 && stat(right, &st2) == 0 && st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino) ? 0 : 1;
            else { fprintf(stderr, "msh: test: %s: unknown operator\n", op); result = 2; }
        } else {
            /* 多参数：简化处理 -a (AND) 和 -o (OR) */
            /* TODO: 完整的 -a/-o 逻辑 */
            result = 1;
        }
        msh_last_status = result;
        return result;
    }
    /* sleep 内建：避免 fork+exec 的延时 */
    if (!strcmp(name, "sleep")) {
        if (argc < 2) { fprintf(stderr, "msh: sleep: missing operand\n"); return 2; }
        for (int i = 1; i < argc; i++) {
            double secs = atof(argv[i]);
            if (secs > 0) {
                unsigned int whole = (unsigned int)secs;
                unsigned long frac = (unsigned long)((secs - whole) * 1e6);
                if (whole > 0) sleep(whole);
                if (frac > 0) usleep(frac);
            }
        }
        msh_last_status = 0;
        return 0;
    }
    /* seq 内建：避免 fork+exec 的序列生成 */
    if (!strcmp(name, "seq")) {
        double start = 1.0, step = 1.0, end;
        int is_int = 1;
        if (argc == 2) { end = atof(argv[1]); }
        else if (argc == 3) { start = atof(argv[1]); end = atof(argv[2]); }
        else if (argc == 4) { start = atof(argv[1]); step = atof(argv[2]); end = atof(argv[3]); }
        else { fprintf(stderr, "msh: seq: usage: seq [start [step]] end\n"); return 2; }
        /* 检查是否整数 */
        for (int i = 1; i < argc; i++) {
            if (strchr(argv[i], '.')) { is_int = 0; break; }
        }
        if (step == 0) { fprintf(stderr, "msh: seq: step cannot be zero\n"); return 1; }
        if (step > 0) {
            for (double v = start; v <= end + 1e-9; v += step) {
                if (is_int) printf("%ld\n", (long)v);
                else printf("%g\n", v);
            }
        } else {
            for (double v = start; v >= end - 1e-9; v += step) {
                if (is_int) printf("%ld\n", (long)v);
                else printf("%g\n", v);
            }
        }
        fflush(stdout);
        msh_last_status = 0;
        return 0;
    }
    (void)ast;
    return 0;
}

/* 把 argv 展开成新的字符串数组（返回 malloc，需 free）。argv 末尾为 NULL。
 * 每个元素先 expand，再 tilde + glob；glob 可能产生多个结果。*/
static char **expand_argv(char **argv, int argc) {
    /* 预估容量：argc + 1 起步，按需扩 */
    size_t cap = (size_t)argc + 1;
    char **r = malloc(sizeof(char *) * cap);
    if (!r) { perror("malloc"); exit(1); }
    size_t n = 0;
    for (int i = 0; i < argc; i++) {
        int cnt = 0;
        char **seg = expand_one_arg(argv[i], &cnt);
        for (int k = 0; k < cnt; k++) {
            if (n + 1 >= cap) {
                cap *= 2;
                r = realloc(r, sizeof(char *) * cap);
            }
            r[n++] = seg[k];
        }
        free(seg);
    }
    r[n] = NULL;
    return r;
}

static void free_argv(char **argv) {
    if (!argv) return;
    for (int i = 0; argv[i]; i++) free(argv[i]);
    free(argv);
}

/* fork+execwait 一个简单命令，应用其 redirects。 */
static int run_simple_cmd(ast_t *cmd) {
    int argc = cmd->argc;
    char **argv_orig = cmd->argv;
    if (argc == 0) return 0;

    char **argv = expand_argv(argv_orig, argc);

    /* 应用重定向（含展开目标） */
    int saved_in = -1, saved_out = -1, saved_err = -1;
    int did_redirect = 0;
    int heredoc_pipe[2] = { -1, -1 };
    for (redirect_t *r = cmd->redir; r; r = r->next) {
        char *target = expand_string(r->target);
        int fd = -1;
        if (r->op == TOK_HERESTRING) {
            /* here-string：target 展开后作为 stdin */
            char *hcontent = expand_string(r->target);
            /* 追加换行（bash 行为） */
            size_t hlen = strlen(hcontent);
            char *with_nl = malloc(hlen + 2);
            memcpy(with_nl, hcontent, hlen);
            with_nl[hlen] = '\n';
            with_nl[hlen + 1] = '\0';
            free(hcontent);
            if (pipe(heredoc_pipe) < 0) {
                perror("pipe"); free(target); free(with_nl); free_argv(argv); return 1;
            }
            write(heredoc_pipe[1], with_nl, hlen + 1);
            free(with_nl);
            close(heredoc_pipe[1]);
            if (saved_in < 0) saved_in = dup(0);
            if (dup2(heredoc_pipe[0], 0) < 0) { perror("dup2"); close(heredoc_pipe[0]); free(target); free_argv(argv); return 1; }
            close(heredoc_pipe[0]);
            heredoc_pipe[0] = -1;
            free(target);
            did_redirect = 1;
            continue;
        }
        if (r->op == TOK_HEREDOC && r->heredoc) {
            /* heredoc：创建管道，写入内容，读端作为 stdin */
            if (pipe(heredoc_pipe) < 0) {
                perror("pipe"); free(target); free_argv(argv); return 1;
            }
            /* 非引号 delimiter 时展开变量/命令替换 */
            char *hcontent = r->heredoc;
            char *hexpanded = NULL;
            if (!r->fd) {
                hexpanded = expand_string(hcontent);
                hcontent = hexpanded;
            }
            /* 写heredoc内容（可能很大，这里简化为一次性写入） */
            size_t hlen = strlen(hcontent);
            if (hlen > 0) {
                ssize_t written = write(heredoc_pipe[1], hcontent, hlen);
                if (written < 0) perror("write heredoc");
            }
            free(hexpanded);
            close(heredoc_pipe[1]);  /* 写完关闭写端 */
            fd = heredoc_pipe[0];    /* 读端作为重定向 stdin */
            r->op = TOK_REDIR_IN;    /* 接下来当作 < 处理 */
            free(target);
            /* 立即应用 stdin 重定向 */
            if (saved_in < 0) saved_in = dup(0);
            if (dup2(fd, 0) < 0) { perror("dup2"); close(fd); free_argv(argv); return 1; }
            close(fd);
            heredoc_pipe[0] = -1;
            did_redirect = 1;
            continue;
        }
        switch (r->op) {
        case TOK_REDIR_IN:     fd = open(target, O_RDONLY); break;
        case TOK_REDIR_OUT:    fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0644); break;
        case TOK_REDIR_APPEND: fd = open(target, O_WRONLY | O_CREAT | O_APPEND, 0644); break;
        case TOK_REDIR_DUP_IN:
        case TOK_REDIR_DUP_OUT: {
            /* >&N 或 <&N：复制 fd N */
            int src = target ? atoi(target) : -1;
            if (src < 0 || src > 255) {
                fprintf(stderr, "%s: %s: bad file descriptor\n", "msh", r->target);
                free(target);
                free_argv(argv);
                return 1;
            }
            int dfd = dup(src);
            if (dfd < 0) {
                fprintf(stderr, "%s: %s: %s\n", "msh", r->target, strerror(errno));
                free(target);
                free_argv(argv);
                return 1;
            }
            int tfd = (r->op == TOK_REDIR_DUP_IN) ? 0 : 1;
            if (tfd == 0 && saved_in < 0) saved_in = dup(0);
            else if (tfd == 1 && saved_out < 0) saved_out = dup(1);
            if (dup2(dfd, tfd) < 0) { perror("dup2"); close(dfd); free(target); free_argv(argv); return 1; }
            close(dfd);
            free(target);
            did_redirect = 1;
            continue;
        }
        default: fd = -1; break;
        }
        free(target);
        if (fd < 0) { perror(r->target); free_argv(argv); return 1; }
        int target_fd = (r->op == TOK_REDIR_IN) ? 0 : 1;
        if (target_fd == 0 && saved_in < 0) saved_in = dup(0);
        else if (target_fd == 1 && saved_out < 0) saved_out = dup(1);
        if (dup2(fd, target_fd) < 0) { perror("dup2"); close(fd); free_argv(argv); return 1; }
        close(fd);
        did_redirect = 1;
    }

    int rc;
    if (is_builtin(argv[0])) {
        /* 计算展开后 argc：expand_argv 可能让 argc 变大（glob）或变小（少用） */
        int expanded_argc = 0;
        while (argv[expanded_argc]) expanded_argc++;
        rc = msh_run_builtin(cmd, expanded_argc, argv);
        free_argv(argv);
    } else {
        pid_t pid = fork();
        if (pid < 0) { rc = -1; free_argv(argv); }
        else if (pid == 0) {
            execvp(argv[0], argv);
            fprintf(stderr, "msh: %s: %s\n", argv[0], strerror(errno));
            _exit(127);
        } else {
            int status;
            if (waitpid(pid, &status, 0) < 0) rc = -1;
            else if (WIFEXITED(status)) rc = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) rc = 128 + WTERMSIG(status);
            else rc = 1;
            free_argv(argv);
        }
    }

    if (saved_in >= 0) { dup2(saved_in, 0); close(saved_in); }
    if (saved_out >= 0) { dup2(saved_out, 1); close(saved_out); }
    (void)saved_err; (void)did_redirect;

    return rc;
}

/* 跑 pipeline：左 -> 右 -> ... */
static int run_pipeline(ast_t *ast) {
    /* 收集链 */
    int stages = 0;
    ast_t *cur = ast;
    while (cur && cur->type == AST_PIPE) {
        stages++;
        cur = cur->left;  /* 计数 */
    }
    if (!cur) return -1;
    stages++;  /* 当前 cur 也是阶段 */

    int n = stages;
    int (*pipes)[2] = calloc(n - 1, sizeof(int));
    pid_t *pids = calloc(n, sizeof(pid_t));
    ast_t **stage = calloc(n, sizeof(ast_t *));

    /* 重新构造 stage 数组 */
    cur = ast;
    int idx = n - 1;
    while (cur && cur->type == AST_PIPE && idx > 0) {
        stage[idx] = cur->right;
        cur = cur->left;
        idx--;
    }
    if (cur) stage[0] = cur;

    /* 创建管道 */
    for (int i = 0; i < n - 1; i++) {
        if (pipe(pipes[i]) < 0) { perror("pipe"); return -1; }
    }

    int last_status = 0;
    for (int i = 0; i < n; i++) {
        pids[i] = fork();
        if (pids[i] < 0) { perror("fork"); return -1; }
        if (pids[i] == 0) {
            /* child */
            if (i > 0) {
                dup2(pipes[i - 1][0], 0);
                close(pipes[i - 1][0]);
                close(pipes[i - 1][1]);
            }
            if (i < n - 1) {
                close(pipes[i][0]);
                dup2(pipes[i][1], 1);
                close(pipes[i][1]);
            }
            for (int j = 0; j < n - 1; j++) {
                if (j != i - 1 && j != i) {
                    if (pipes[j][0] >= 0) close(pipes[j][0]);
                    if (pipes[j][1] >= 0) close(pipes[j][1]);
                }
            }
            ast_t *stg = stage[i];
            if (stg->type != AST_CMD || stg->argc == 0) _exit(0);
            char **argv = expand_argv(stg->argv, stg->argc);
            if (!argv || !argv[0]) _exit(0);
            /* 重定向（含展开目标） */
            for (redirect_t *r = stg->redir; r; r = r->next) {
                char *target = expand_string(r->target);
                int fd;
                switch (r->op) {
                case TOK_REDIR_IN:    fd = open(target, O_RDONLY); break;
                case TOK_REDIR_OUT:   fd = open(target, O_WRONLY | O_CREAT | O_TRUNC, 0644); break;
                case TOK_REDIR_APPEND: fd = open(target, O_WRONLY | O_CREAT | O_APPEND, 0644); break;
                case TOK_REDIR_DUP_IN:
                case TOK_REDIR_DUP_OUT: {
                    int src = target ? atoi(target) : -1;
                    if (src < 0 || src > 255) {
                        fprintf(stderr, "%s: %s: bad file descriptor\n", "msh", r->target);
                        free(target); free_argv(argv); _exit(1);
                    }
                    int dfd = dup(src);
                    if (dfd < 0) { perror(r->target); free(target); free_argv(argv); _exit(1); }
                    int tfd = (r->op == TOK_REDIR_DUP_IN) ? 0 : 1;
                    if (dup2(dfd, tfd) < 0) _exit(1);
                    close(dfd);
                    free(target);
                    continue;
                }
                default: fd = -1; break;
                }
                free(target);
                if (fd < 0) { perror(r->target); _exit(1); }
                int tfd = (r->op == TOK_REDIR_IN) ? 0 : 1;
                if (dup2(fd, tfd) < 0) _exit(1);
                close(fd);
            }
            if (is_builtin(argv[0])) {
                int expanded_argc = 0;
                while (argv[expanded_argc]) expanded_argc++;
                int rc = msh_run_builtin(stg, expanded_argc, argv);
                free_argv(argv);
                _exit(rc);
            }
            execvp(argv[0], argv);
            fprintf(stderr, "msh: %s: %s\n", argv[0], strerror(errno));
            free_argv(argv);
            _exit(127);
        }
    }
    /* 父进程关管道 fd */
    for (int i = 0; i < n - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }
    for (int i = 0; i < n; i++) {
        int status;
        waitpid(pids[i], &status, 0);
        if (i == n - 1 && WIFEXITED(status)) last_status = WEXITSTATUS(status);
    }
    free(pipes); free(pids); free(stage);
    return last_status;
}

int msh_eval(ast_t *ast) {
    if (!ast) return 0;
    /* 每次进入 eval 时检查 pending trap（命令间隙执行 trap）。*/
    msh_trap_check();
    if (ast->type == AST_ASSIGN) {
        /* 执行前导赋值 */
        for (ast_t *cur = ast; cur; cur = cur->right) {
            if (cur->type != AST_ASSIGN) {
                /* 链到命令 */
                return msh_eval(cur);
            }
            if (!cur->argv || !cur->argv[0]) continue;
            char *eq = strchr(cur->argv[0], '=');
            if (!eq) continue;
            /* Check for bash array syntax: arr=(...) or arr[idx]=val */
            if (eq[1] == '(') {
                /* arr=(elem1 elem2 ...) */
                msh_array_parse_assign(cur->argv[0]);
                continue;
            }
            /* Check for arr[idx]=val */
            const char *lb = strchr(cur->argv[0], '[');
            if (lb && lb < eq) {
                msh_array_parse_indexed(cur->argv[0]);
                continue;
            }
            /* 注意：函数体 AST 可能被多个嵌套调用共享，不能原地修改 argv[0]。
             * 用 strndup 拷贝 name 和 RHS。 */
            size_t name_len = (size_t)(eq - cur->argv[0]);
            char *name = strndup(cur->argv[0], name_len);
            char *val = expand_string(eq + 1);
            setenv(name, val ? val : "", 1);
            free(val);
            free(name);
        }
        return 0;
    }
    if (ast->type == AST_CMD) {
        /* 别名展开（非 classic 模式）：ll -> ls -l */
        if (!msh_mode_classic && ast->argv && ast->argv[0]) {
            const char *alias = msh_alias_lookup(ast->argv[0]);
            if (alias) {
                /* 分词别名值（空白分隔），构造新 argv：alias_words + 原 argv[1..] */
                int alias_argc = 0;
                char *alias_words[64];
                char *copy = strdup(alias);
                char *tok = strtok(copy, " \t");
                while (tok && alias_argc < 64) {
                    alias_words[alias_argc++] = tok;
                    tok = strtok(NULL, " \t");
                }
                /* 检查是否真的产生多个词（单词别名等价于替换 argv[0]） */
                if (alias_argc > 0) {
                    int total = alias_argc + (ast->argc - 1);
                    char **newargv = malloc(sizeof(char *) * (total + 1));
                    int k = 0;
                    for (int a = 0; a < alias_argc; a++) newargv[k++] = strdup(alias_words[a]);
                    for (int a = 1; a < ast->argc; a++) newargv[k++] = ast->argv[a];
                    newargv[k] = NULL;
                    /* 替换 ast 的 argv（旧的 argv[0] 属于 ast 内存，不能 free 冲突）
                     * 简化：仅当别名展开后是单个词时替换 argv[0]；
                     * 多词则用 AST_CMD 的 argv 指向新数组（内存泄漏可接受，脚本执行一次） */
                    if (alias_argc == 1) {
                        free(newargv[0]);
                        newargv[0] = NULL;
                        free(newargv);
                        char *old0 = ast->argv[0];
                        ast->argv[0] = strdup(alias_words[0]);
                        free(old0);
                    } else {
                        /* 多词：直接替换整个 argv（释放旧的 argv 数组但保留 argv[0] 引用…简化：泄漏旧的） */
                        char **oldargv = ast->argv;
                        ast->argv = newargv;
                        ast->argc = total;
                        free(oldargv[0]);
                        free(oldargv);
                    }
                }
                free(copy);
            }
        }
        /* 函数优先于外部命令：argv[0] 在函数表里则在当前进程执行函数体 */
        if (ast->argv && ast->argv[0]) {
            char **argv = expand_argv(ast->argv, ast->argc);
            ast_t *fn = func_lookup(argv[0]);
            if (fn) {
                /* 保存/恢复位置参数 $1..$n */
                /* 简化：用环境变量传位置参数 */
                char **saved = NULL;
                int saved_n = 0;
                char buf[32];
                /* 先保存旧的 $1..$63 */
                for (int i = 1; i < 64; i++) {
                    char vn[16];
                    snprintf(vn, sizeof(vn), "%d", i);
                    const char *v = getenv(vn);
                    if (v) {
                        saved = realloc(saved, sizeof(char *) * (saved_n + 1));
                        char namebuf[16];
                        snprintf(namebuf, sizeof(namebuf), "%d", i);
                        /* 同时记 name+value 便于还原 -- 简化：仅还原 1..n */
                        (void)namebuf;
                        saved[saved_n++] = strdup(v);
                    } else break;
                }
                /* 设新位置参数：跳过 argv[0]，从 argv[1] 开始 */
                int new_argc = ast->argc;
                for (int i = 1; i < 64; i++) {
                    char vn[16];
                    snprintf(vn, sizeof(vn), "%d", i);
                    if (i < new_argc && argv[i]) {
                        setenv(vn, argv[i], 1);
                    } else {
                        unsetenv(vn);
                    }
                }
                /* $# */
                snprintf(buf, sizeof(buf), "%d", new_argc - 1);
                setenv("#", buf, 1);
                int rc = msh_eval(fn);
                /* 函数返回后重置 return 标志，防止上层 AST_LIST 误中断 */
                if (msh_return_flag) {
                    rc = msh_return_value;
                    msh_return_flag = 0;
                }
                /* 还原旧位置参数 */
                for (int i = 1; i <= saved_n; i++) {
                    char vn[16];
                    snprintf(vn, sizeof(vn), "%d", i);
                    setenv(vn, saved[i - 1], 1);
                }
                for (int i = saved_n + 1; i < 64; i++) {
                    char vn[16];
                    snprintf(vn, sizeof(vn), "%d", i);
                    unsetenv(vn);
                }
                for (int i = 0; i < saved_n; i++) free(saved[i]);
                free(saved);
                free_argv(argv);
                msh_last_status = rc;
                return rc;
            }
            free_argv(argv);
        }
        int rc = run_simple_cmd(ast);
        msh_last_status = rc;
        if (msh_errexit && !msh_in_cond && rc != 0) {
            fprintf(stderr, "msh: set -e: command failed (exit %d)\n", rc);
            exit(rc);
        }
        return rc;
    }
    if (ast->type == AST_PIPE) {
        int rc = run_pipeline(ast);
        if (msh_errexit && !msh_in_cond && rc != 0) {
            fprintf(stderr, "msh: set -e: pipeline failed (exit %d)\n", rc);
            exit(rc);
        }
        return rc;
    }
    if (ast->type == AST_LIST) {
        int rc = msh_eval(ast->left);
        /* 检查控制流标志：return/break/continue 后不执行后续命令 */
        if (msh_return_flag || msh_break_flag || msh_continue_flag) return rc;
        int proceed = 1;
        int saved = msh_in_cond; msh_in_cond = 1;
        if (ast->list_op == TOK_AND && rc != 0) proceed = 0;
        if (ast->list_op == TOK_OR && rc == 0) proceed = 0;
        msh_in_cond = saved;
        if (!proceed) return rc;
        return msh_eval(ast->right);
    }
    if (ast->type == AST_IF) {
        int saved = msh_in_cond; msh_in_cond = 1;
        int rc = msh_eval(ast->cond);
        msh_in_cond = saved;
        if (rc == 0) return msh_eval(ast->then_body);
        if (ast->else_body) return msh_eval(ast->else_body);
        return rc;
    }
    if (ast->type == AST_WHILE) {
        int rc = 0;
        int saved = msh_in_cond; msh_in_cond = 1;
        msh_loop_depth++;
        while (msh_eval(ast->cond) == 0) {
            msh_in_cond = saved;
            if (msh_return_flag) break;
            rc = msh_eval(ast->body);
            msh_in_cond = saved;
            if (msh_return_flag) break;
            if (msh_break_flag) { msh_break_flag--; break; }
            if (msh_continue_flag) { msh_continue_flag--; continue; }
            if (msh_last_status == 256 + SIGINT) break;
        }
        msh_loop_depth--;
        return rc;
    }
    if (ast->type == AST_UNTIL) {
        int rc = 0;
        int saved = msh_in_cond; msh_in_cond = 1;
        msh_loop_depth++;
        while (msh_eval(ast->cond) != 0) {
            msh_in_cond = saved;
            if (msh_return_flag) break;
            rc = msh_eval(ast->body);
            msh_in_cond = saved;
            if (msh_return_flag) break;
            if (msh_break_flag) { msh_break_flag--; break; }
            if (msh_continue_flag) { msh_continue_flag--; continue; }
        }
        msh_loop_depth--;
        return rc;
    }
    if (ast->type == AST_FOR) {
        int rc = 0;
        char varname[64];
        snprintf(varname, sizeof(varname), "%s", ast->str_val ? ast->str_val : "i");
        msh_loop_depth++;
        if (ast->npatterns > 0) {
            for (int i = 0; i < ast->npatterns; i++) {
                setenv(varname, ast->patterns[i], 1);
                if (msh_return_flag) break;
                rc = msh_eval(ast->body);
                if (msh_return_flag) break;
                if (msh_break_flag) { msh_break_flag--; break; }
                if (msh_continue_flag) { msh_continue_flag--; continue; }
            }
        } else {
            const char *cnt = getenv("#");
            int n = cnt ? atoi(cnt) : 0;
            for (int i = 1; i <= n; i++) {
                char vn[16];
                snprintf(vn, sizeof(vn), "%d", i);
                const char *v = getenv(vn);
                if (v) { setenv(varname, v, 1); }
                if (msh_return_flag) break;
                if (v) { rc = msh_eval(ast->body); }
                if (msh_return_flag) break;
                if (msh_break_flag) { msh_break_flag--; break; }
                if (msh_continue_flag) { msh_continue_flag--; continue; }
            }
        }
        msh_loop_depth--;
        return rc;
    }
    if (ast->type == AST_FUNC) {
        func_add(ast->str_val, ast->body);
        return 0;
    }
    if (ast->type == AST_BRACE_GROUP) {
        return msh_eval(ast->body);
    }
    if (ast->type == AST_SUBSHELL) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return 1; }
        if (pid == 0) {
            int rc = msh_eval(ast->body);
            _exit(rc);
        }
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) return WEXITSTATUS(status);
        if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
        return 1;
    }
    if (ast->type == AST_BG) {
        /* 后台执行：fork 子进程执行 left，父进程登记作业后立即返回 0 */
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return 1; }
        if (pid == 0) {
            /* 子进程：后台执行，脱离终端控制（简化：忽略 SIGINT/SIGQUIT） */
            signal(SIGINT, SIG_IGN);
            signal(SIGQUIT, SIG_IGN);
            int rc = msh_eval(ast->left);
            _exit(rc);
        }
        /* 登记作业（取命令行可读文本） */
        char cmdline[256] = "";
        if (ast->left && ast->left->type == AST_CMD && ast->left->argv && ast->left->argv[0]) {
            size_t off = 0;
            for (int i = 0; i < ast->left->argc && off < sizeof(cmdline) - 1; i++) {
                int n = snprintf(cmdline + off, sizeof(cmdline) - off, "%s%s",
                                 i ? " " : "", ast->left->argv[i]);
                if (n < 0) break;
                off += (size_t)n;
            }
        }
        msh_job_add(pid, cmdline);
        return 0;
    }
    if (ast->type == AST_CASE) {
        char *word = expand_string(ast->str_val);
        for (int i = 0; i < ast->npatterns; i++) {
            if (fnmatch(ast->patterns[i], word ? word : "", 0) == 0) {
                int rc = msh_eval(ast->items[i]);
                free(word);
                return rc;
            }
        }
        free(word);
        return 0;
    }
    return 0;
}
