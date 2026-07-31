/* msh/parse.h — 语法分析器公共 API
 *
 * 解析词法 token 流为 AST，然后由 exec 求值。
 *
 * 使用：
 *   parser_t p;
 *   msh_parser_init(&p, lexer);
 *   ast_t *tree = msh_parse(&p);
 *   msh_eval(tree);  // 来自 exec.h
 *   ast_free(tree);
 *   msh_parser_free(&p);
 */
#ifndef MEUOS_MSH_PARSE_H
#define MEUOS_MSH_PARSE_H

#include <stddef.h>

#include "msh/msh.h"
#include "msh/lex.h"

#ifdef __cplusplus
extern "C" {
#endif

/* AST 节点类型 */
enum {
    AST_WORD,         /* 字符串 */
    AST_ASSIGN,       /* VAR=value */
    AST_CMD,          /* argv[] + redirects[] */
    AST_PIPE,         /* left | right */
    AST_LIST,         /* op = ; | && | || */
    AST_BG,           /* & */
    AST_SUBSHELL,     /* ( ... ) */
    AST_IF,           /* if ... then ... else ... fi */
    AST_FOR,          /* for x in ...; do ...; done */
    AST_WHILE,        /* while ... do ... done */
    AST_FUNC,         /* fname() { ... } */
    AST_UNTIL,        /* until ... do ... done */
    AST_CASE,         /* case word in pat) cmd ;; esac */
    AST_BRACE_GROUP,  /* { ...; } */
};

/* 重定向 */
struct redirect {
    int op;            /* TOK_REDIR_IN/OUT/APPEND 等 */
    char *target;      /* 文件名或 here-doc EOF */
    char *heredoc;     /* here-doc 文本（如有） */
    int fd;
    struct redirect *next;
};

/* AST 节点 */
struct ast {
    int type;
    struct ast *left;       /* 子节点 */
    struct ast *right;      /* 兄弟节点（在 list 中） */
    int list_op;            /* 与 right 之间的连接符；AST_LIST 用 */
    /* 通用 */
    int argc;
    char **argv;
    /* 重定向链表 */
    struct redirect *redir;
    /* 控制流/函数/for/case 共用字段 */
    char *str_val;          /* AST_FOR: varname; AST_FUNC: fname; AST_CASE: word */
    struct ast *cond;       /* AST_IF/WHILE/UNTIL: 条件体 */
    struct ast *then_body;  /* AST_IF: then 分支 */
    struct ast *else_body;  /* AST_IF: else/elif 链 */
    struct ast *body;       /* AST_FOR/WHILE/UNTIL/FUNC/BRACE_GROUP/SUBSHELL: 主体 */
    struct ast **items;     /* AST_FOR: wordlist; AST_CASE: 各分支 body */
    int nitems;
    char **patterns;        /* AST_CASE: 各分支 pattern（与 items 对齐） */
    int npatterns;
};

typedef struct ast ast_t;
typedef struct redirect redirect_t;

/* 一次性解析一段 token 流为 AST。 */
ast_t *msh_parse(lexer_t *lx);

/* 求值 AST。fork/exec/pipeline/redirects 全由本模块完成。
 * 返回最后一条命令的退出码。 */
int msh_eval(ast_t *ast);

/* 释放 AST。 */
void ast_free(ast_t *a);

/* 内建命令表入口（供 builtin.c 用） */
int msh_run_builtin(ast_t *ast, int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_MSH_PARSE_H */
