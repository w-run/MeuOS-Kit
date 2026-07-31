/* msh/lex.h — 词法分析器公共 API
 *
 * 现在：暴露词法库的实际接口。
 *
 * 使用：
 *   lexer_t lx;
 *   msh_lexer_init(&lx, input, len);
 *   while (1) {
 *       char *text;
 *       int tok = msh_lex_next(&lx, &text);
 *       // 处理 token
 *       free(text);
 *       if (tok == TOK_EOF) break;
 *   }
 *   msh_lexer_free(&lx);
 */
#ifndef MEUOS_MSH_LEX_H
#define MEUOS_MSH_LEX_H

#include <stddef.h>

#include "msh/msh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 词法器结构（完整定义公开，避免跨 TU 字段访问类型不一致） */
typedef struct lexer {
    const char *src;
    size_t pos;
    size_t len;
    int lineno;
    char *word_buf;
    size_t word_cap;
    size_t word_len;
    int in_word;
    /* 待返回的运算符（当 word_buf 已经被取走后） */
    int pending_op;
    /* 命令起始位置标记：1 表示下一个 WORD 可能是保留字 */
    int at_cmd_start;
} lexer_t;

void msh_lexer_init(lexer_t *lx, const char *src, size_t len);
void msh_lexer_free(lexer_t *lx);

/* Token 类型 ID。 */
enum {
    TOK_EOF = 0,
    TOK_WORD,
    TOK_NEWLINE,
    TOK_SEMI,
    TOK_BG,
    TOK_AND,
    TOK_OR,
    TOK_PIPE,
    TOK_REDIR_IN,
    TOK_REDIR_OUT,
    TOK_REDIR_APPEND,
    TOK_REDIR_DUP_IN,
    TOK_REDIR_DUP_OUT,
    TOK_HEREDOC,
    TOK_HERESTRING,
    TOK_LPAREN,
    TOK_RPAREN,
    /* 保留字（仅在命令起始位置识别） */
    TOK_IF,
    TOK_THEN,
    TOK_ELIF,
    TOK_ELSE,
    TOK_FI,
    TOK_FOR,
    TOK_IN,
    TOK_DO,
    TOK_DONE,
    TOK_WHILE,
    TOK_UNTIL,
    TOK_CASE,
    TOK_ESAC,
    TOK_FUNCTION,
    TOK_LBRACE,    /* { */
    TOK_RBRACE,    /* } */
    TOK_BANG,      /* ! (pipeline 取反) */
};

/* 推进一步 token。返回 token 类型；如 token 有文本，通过 *out_text 返回
 * （调用者 free()）。out_text 可为 NULL。 */
int msh_lex_next(lexer_t *lx, char **out_text);

#ifdef __cplusplus
}
#endif

#endif /* MEUOS_MSH_LEX_H */
